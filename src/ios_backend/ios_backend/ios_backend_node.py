"""IOS Backend — FastAPI + rclpy WebSocket bridge.

Bridges ROS2 topics to/from browser clients via WebSocket.
- Subscribes to /aircraft/fdm/state, /aircraft/fuel/state, /sim/state → forwards as JSON to WS clients
- Discovers nodes via ROS2 graph API, heartbeats, and lifecycle_state messages
- Receives panel commands and sim commands from WS clients
- Calls ROS2 lifecycle services for node management commands
- Sends stub data for topics not yet implemented (avionics, etc.); stubs suppressed when real data arrives
"""

import asyncio
import json
import math
import os
import threading
import time
import numpy as np


class _RosEncoder(json.JSONEncoder):
    """Handle numpy types from ROS2 message arrays."""
    def default(self, obj):
        if isinstance(obj, (np.bool_,)):
            return bool(obj)
        if isinstance(obj, (np.integer,)):
            return int(obj)
        if isinstance(obj, (np.floating,)):
            return float(obj)
        if isinstance(obj, np.ndarray):
            return obj.tolist()
        return super().default(obj)


def _dumps(obj):
    return json.dumps(obj, cls=_RosEncoder)


# asyncio.create_task() returns a Task that must be strongly referenced, or
# Python's GC may cancel it mid-flight. This is documented in the stdlib
# (python.org asyncio-task Task docs). Fire-and-forget spawns that await
# other coroutines (e.g. ROS2 service calls) are especially vulnerable —
# observable symptom: patch add_patch / update_patch silently dropped when
# the backend is busy.
_bg_tasks: set = set()


def _spawn(coro):
    """Fire-and-forget task spawn that holds a reference until completion."""
    t = asyncio.create_task(coro)
    _bg_tasks.add(t)
    t.add_done_callback(_bg_tasks.discard)
    return t


import yaml

import rclpy
from rclpy.node import Node
from rclpy.parameter import Parameter

from sim_msgs.msg import (FlightModelState, FuelState, SimState, SimCommand, PanelControls,
                          NavigationState, AvionicsControls, RawAvionicsControls,
                          RawFlightControls, RawEngineControls,
                          ElectricalState, SimAlert, EngineState,
                          FailureCommand, FailureState, TerrainSource,
                          AirDataState, PayloadCommand,
                          ArbitrationState, GearState,
                          AtmosphereState, WeatherState, WeatherWindLayer,
                          WeatherCloudLayer, MicroburstHazard, WeatherPatch)
from std_msgs.msg import String
from lifecycle_msgs.srv import ChangeState
from lifecycle_msgs.msg import Transition
from sim_msgs.srv import SearchAirports, GetRunways, SearchNavaids, GetTerrainElevation

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Query
from fastapi.responses import JSONResponse
import uvicorn

from .topic_forwarder import TopicForwarder


# ─────────────────────────────────────────────────────────────────────────────
# Training-plausible bounds for weather authoring. Frontend sliders and
# backend validation share this table. Any field outside these ranges is
# rejected with a descriptive error returned to the WS client. CIGI protocol
# limits are wider than these; we clamp to instructor-realistic ranges.
#
# Units listed are WS-message units (what the frontend sends), not wire units.
# Conversion to wire units (K, Pa, m/s, m) happens inside publish_weather().
# ─────────────────────────────────────────────────────────────────────────────
WEATHER_BOUNDS = {
    # Global atmosphere
    'temperature_c':       (-100.0,     60.0),
    'pressure_hpa':        ( 880.0,   1075.0),
    'pressure_sl_pa':      (80000.0, 110000.0),
    'visibility_m':        (   0.0, 160000.0),
    'humidity_pct':        (   0,       100),

    # Wind (per layer)
    'wind_altitude_ft':    (   0.0,  45000.0),
    'wind_speed_kt':       (   0.0,    100.0),
    'wind_direction_deg':  (   0.0,    359.9),
    'wind_vertical_ms':    ( -10.0,     10.0),
    'wind_shear_kt':       (   0.0,     50.0),
    'turbulence':          (   0.0,     10.0),

    # Clouds (per layer) — altitudes in ft MSL
    'cloud_base_ft':       (   0.0,  45000.0),
    'cloud_top_ft':        (   0.0,  50000.0),
    'cloud_coverage_pct':  (   0.0,    100.0),
    'scud_frequency_pct':  (   0.0,    100.0),
    'cloud_type':          (   0,         3),

    # Precipitation
    'precipitation_rate':  (   0.0,      1.0),
    'precipitation_type':  (   0,         3),

    # Patch geometry
    'radius_nm':           (   1.0,     50.0),

    # Runway condition is validated inline (0-15 integer index) in
    # _validate_weather_bounds; the generic range check is not used for it.

    # Wave
    'wave_height_m':       (   0.0,     10.0),
    'wave_direction_deg':  (   0.0,    359.9),

    # Microburst
    'microburst_intensity_ms':  ( 0.0,    50.0),
    'microburst_core_radius_m': ( 100.0, 2000.0),
    'microburst_shaft_alt_m':   ( 100.0, 5000.0),
    'microburst_lifecycle':     ( 0.0,      1.0),
}


def _validate_in_range(field_name: str, value, bounds_key: str):
    """Raise ValueError if value is outside WEATHER_BOUNDS[bounds_key].
    Returns value on success. None means "not provided" — skip validation.
    """
    lo, hi = WEATHER_BOUNDS[bounds_key]
    if value is None:
        return value
    if not (lo <= value <= hi):
        raise ValueError(
            f"{field_name}={value} out of range [{lo}, {hi}] "
            f"(bounds key: {bounds_key})"
        )
    return value


# ── Patch lifecycle validators (Slice 5c-refactor-I) ────────────────────────
# Called by reserve_patch / update_patch_identity / update_patch_overrides.
# Identity and overrides arrive on separate WS messages and validate
# independently.

def _validate_identity(data: dict, require_all: bool = True) -> dict:
    """Validate the identity subset of a patch payload.

    require_all=True (reserve path): patch_type, lat_deg/lon_deg or icao,
    and radius_m must be present.
    require_all=False (update_patch_identity): only validates fields that
    are present; patch_id is the anchor. Missing fields mean "unchanged".
    """
    if 'patch_type' in data:
        patch_type = data['patch_type']
        if patch_type not in ('airport', 'custom'):
            raise ValueError(f"patch_type must be 'airport' or 'custom', got {patch_type!r}")
    elif require_all:
        raise ValueError('patch_type required')

    # ICAO shape check (airport patches); empty OK for custom.
    if 'icao' in data:
        icao = (data.get('icao') or '').strip().upper()
        if icao and (len(icao) > 4 or not icao.isalnum()):
            raise ValueError(f"icao must be up to 4 alphanumeric chars, got {icao!r}")
        data['icao'] = icao
    elif require_all and data.get('patch_type') == 'airport':
        raise ValueError('airport patch requires icao')

    if 'lat_deg' in data:
        lat = float(data['lat_deg'])
        if not (-90.0 <= lat <= 90.0):
            raise ValueError(f"lat_deg out of range: {lat}")
    elif require_all and data.get('patch_type') == 'custom':
        raise ValueError('custom patch requires lat_deg')

    if 'lon_deg' in data:
        lon = float(data['lon_deg'])
        if not (-180.0 <= lon <= 180.0):
            raise ValueError(f"lon_deg out of range: {lon}")
    elif require_all and data.get('patch_type') == 'custom':
        raise ValueError('custom patch requires lon_deg')

    if 'radius_m' in data:
        radius_nm = float(data['radius_m']) / 1852.0
        _validate_in_range('radius_m->nm', radius_nm, 'radius_nm')
    elif require_all:
        raise ValueError('radius_m required')

    return data


def _validate_overrides(data: dict) -> dict:
    """Validate the override + layers subset of a patch payload. All fields
    optional — only present fields are range-checked.
    """
    for i, cl in enumerate(data.get('cloud_layers', [])):
        _validate_in_range(f"cloud[{i}].cloud_type",   cl.get('cloud_type'),   'cloud_type')
        _validate_in_range(f"cloud[{i}].coverage_pct", cl.get('coverage_pct'), 'cloud_coverage_pct')
        base_m = cl.get('base_elevation_m')
        if base_m is not None:
            _validate_in_range(f"cloud[{i}].base_ft", base_m * 3.28084, 'cloud_base_ft')
        thick_m = cl.get('thickness_m')
        if base_m is not None and thick_m is not None:
            _validate_in_range(f"cloud[{i}].top_ft", (base_m + thick_m) * 3.28084, 'cloud_top_ft')
        _validate_in_range(f"cloud[{i}].scud_freq", cl.get('scud_frequency_pct'), 'scud_frequency_pct')

    for i, wl in enumerate(data.get('wind_layers', [])):
        alt_m = wl.get('altitude_msl_m')
        if alt_m is not None:
            _validate_in_range(f"wind[{i}].alt_ft", alt_m * 3.28084, 'wind_altitude_ft')
        spd_ms = wl.get('wind_speed_ms')
        if spd_ms is not None:
            _validate_in_range(f"wind[{i}].speed_kt", spd_ms / 0.51444, 'wind_speed_kt')
        _validate_in_range(f"wind[{i}].direction", wl.get('wind_direction_deg'), 'wind_direction_deg')
        _validate_in_range(f"wind[{i}].vertical", wl.get('vertical_wind_ms'), 'wind_vertical_ms')

    if data.get('override_visibility'):
        _validate_in_range('visibility_m', data.get('visibility_m'), 'visibility_m')
    if data.get('override_temperature'):
        temp_k = data.get('temperature_k')
        if temp_k is not None:
            _validate_in_range('temperature_c', float(temp_k) - 273.15, 'temperature_c')
    if data.get('override_precipitation'):
        _validate_in_range('precip_rate', data.get('precipitation_rate'), 'precipitation_rate')
        _validate_in_range('precip_type', data.get('precipitation_type'), 'precipitation_type')
    if data.get('override_humidity'):
        _validate_in_range('humidity_pct', data.get('humidity_pct'), 'humidity_pct')
    if data.get('override_pressure'):
        _validate_in_range('pressure_sl_pa', data.get('pressure_sl_pa'), 'pressure_sl_pa')
    if data.get('override_runway'):
        rci = data.get('runway_condition_idx')
        if rci is None or not (0 <= int(rci) <= 15):
            raise ValueError(f"runway_condition_idx out of range [0, 15]: {rci}")

    return data



def _safe_callback(fn):
    """Decorator: catch exceptions in ROS2 subscription callbacks so spin_once survives."""
    from functools import wraps
    @wraps(fn)
    def wrapper(self, msg):
        try:
            fn(self, msg)
        except Exception as e:
            self.get_logger().warn(f'[{fn.__name__}] callback error: {e}')
    return wrapper


# ── ROS2 Node ────────────────────────────────────────────────────────────────

class IosBackendNode(Node):
    def __init__(self):
        super().__init__('ios_backend', parameter_overrides=[
            Parameter('use_sim_time', Parameter.Type.BOOL, True),
        ])
        self._latest = {}
        self._lock = threading.Lock()
        self._loaded_aircraft_id = ''
        self._active_microbursts = []   # list of MicroburstHazard-like dicts
        self._mb_next_id = 1
        self._last_weather_data = {}    # cache last set_weather data for republish

        # WeatherPatch lifecycle (Slice 4). Patches are dicts, patch_id is
        # monotonic uint16 owned by this process, no reuse within a session.
        self._active_patches = []
        self._next_patch_id = 1

        self._flight_model_sub = self.create_subscription(
            FlightModelState, '/aircraft/fdm/state', self._on_flight_model_state, 10)
        self._fuel_sub = self.create_subscription(
            FuelState, '/aircraft/fuel/state', self._on_fuel_state, 10)
        self._sim_state_sub = self.create_subscription(
            SimState, '/sim/state', self._on_sim_state, 10)
        self._nav_state_sub = self.create_subscription(
            NavigationState, '/aircraft/navigation/state', self._on_nav_state, 10)
        self._avionics_sub = self.create_subscription(
            AvionicsControls, '/aircraft/controls/avionics', self._on_avionics_controls, 10)
        self._elec_sub = self.create_subscription(
            ElectricalState, '/aircraft/electrical/state', self._on_electrical_state, 10)
        self._alert_sub = self.create_subscription(
            SimAlert, '/sim/alerts', self._on_sim_alert, 10)
        self._engine_sub = self.create_subscription(
            EngineState, '/aircraft/engines/state', self._on_engine_state, 10)
        self._heartbeat_sub = self.create_subscription(
            String, '/sim/diagnostics/heartbeat', self._on_heartbeat, 10)
        self._lifecycle_state_sub = self.create_subscription(
            String, '/sim/diagnostics/lifecycle', self._on_lifecycle_state, 10)

        # Failure state subscription
        self._failure_state_sub = self.create_subscription(
            FailureState, '/sim/failures/state', self._on_failure_state, 10)

        # Terrain source subscription
        self._terrain_source_sub = self.create_subscription(
            TerrainSource, '/sim/terrain/source', self._on_terrain_source, 10)

        # Air data subscription (pitot-static instruments)
        self._air_data_sub = self.create_subscription(
            AirDataState, '/aircraft/air_data/state', self._on_air_data_state, 10)

        # Arbitration state (per-channel input source)
        self._arbitration_sub = self.create_subscription(
            ArbitrationState, '/aircraft/controls/arbitration', self._on_arbitration_state, 10)

        # Gear state
        self._gear_sub = self.create_subscription(
            GearState, '/aircraft/gear/state', self._on_gear_state, 10)

        # Atmosphere state (from weather_solver)
        self._atmosphere_sub = self.create_subscription(
            AtmosphereState, '/world/atmosphere', self._on_atmosphere_state, 10)

        # Failure command publisher → sim_failures
        self._failure_cmd_pub = self.create_publisher(
            FailureCommand, '/aircraft/devices/instructor/failure_command', 10)

        # IOS instructor panel — highest priority in arbitrator
        self._panel_pub = self.create_publisher(
            PanelControls, '/aircraft/devices/instructor/panel', 10)
        # Virtual cockpit panel — lower priority, used by /cockpit/* pages
        self._virtual_panel_pub = self.create_publisher(
            PanelControls, '/aircraft/devices/virtual/panel', 10)
        self._raw_avionics_pub = self.create_publisher(
            RawAvionicsControls, '/aircraft/devices/instructor/controls/avionics', 10)
        self._virtual_avionics_pub = self.create_publisher(
            RawAvionicsControls, '/aircraft/devices/virtual/controls/avionics', 10)
        self._cmd_pub = self.create_publisher(
            SimCommand, '/sim/command', 10)
        # Instructor flight/engine controls — arbitrated by input_arbitrator.
        # INSTRUCTOR priority is sticky: once published, this source locks the
        # channel until node reconfigure. Used for explicit IOS overrides.
        self._raw_flight_pub = self.create_publisher(
            RawFlightControls, '/aircraft/devices/instructor/controls/flight', 10)
        self._raw_engine_pub = self.create_publisher(
            RawEngineControls, '/aircraft/devices/instructor/controls/engine', 10)
        # Virtual flight/engine controls — used by the virtual cockpit page.
        # VIRTUAL priority is non-sticky and sits below HARDWARE/INSTRUCTOR in
        # the arbitrator, so IOS can still take over when needed.
        self._raw_flight_virt_pub = self.create_publisher(
            RawFlightControls, '/aircraft/devices/virtual/controls/flight', 10)
        self._raw_engine_virt_pub = self.create_publisher(
            RawEngineControls, '/aircraft/devices/virtual/controls/engine', 10)
        self._payload_pub = self.create_publisher(
            PayloadCommand, '/aircraft/payload/command', 10)
        self._fuel_load_pub = self.create_publisher(
            PayloadCommand, '/aircraft/fuel/load_command', 10)
        self._weather_pub = self.create_publisher(
            WeatherState, '/world/weather', 10)
        self._heartbeat_pub = self.create_publisher(
            String, '/sim/diagnostics/heartbeat', 10)
        self._lifecycle_pub = self.create_publisher(
            String, '/sim/diagnostics/lifecycle', 10)

        # Service clients for airport/runway queries
        self._search_airports_cli = self.create_client(
            SearchAirports, '/navaid_sim/search_airports')
        self._get_runways_cli = self.create_client(
            GetRunways, '/navaid_sim/get_runways')
        self._search_navaids_cli = self.create_client(
            SearchNavaids, '/navaid_sim/search_navaids')
        self._terrain_elevation_cli = self.create_client(
            GetTerrainElevation, '/navaid_sim/get_terrain_elevation')

        # Own heartbeat at 1 Hz
        self._heartbeat_timer = self.create_timer(1.0, self._publish_heartbeat)

        # Report active on startup
        self._publish_lifecycle('active')

        self._cmd_map = {
            1: SimCommand.CMD_RUN,
            2: SimCommand.CMD_FREEZE,
            3: SimCommand.CMD_RESET,          # RESET_FLIGHT
            4: SimCommand.CMD_RESET,          # RESET_AIRCRAFT
            5: SimCommand.CMD_RESET,          # RESET_FAILURES (no dedicated cmd yet)
            6: SimCommand.CMD_SHUTDOWN,
            7: SimCommand.CMD_RELOAD_NODE,
            8: SimCommand.CMD_DEACTIVATE_NODE,
            9: SimCommand.CMD_ACTIVATE_NODE,
            10: SimCommand.CMD_RESET_NODE,
        }

        # Pre-load default aircraft configs
        self._load_avionics_config('c172')
        self._load_engine_config('c172')
        self._load_fuel_config('c172')
        self._load_failures_config('c172')
        self._load_electrical_config('c172')
        self._load_weight_config('c172')

        # Generic topic forwarder for State Inspector (raw SI values)
        self._forwarder = TopicForwarder(self)

        # Seed initial weather_state so new WS clients get defaults on connect
        self._broadcast_weather_state()

        self.get_logger().info('ios_backend started — ws://0.0.0.0:8080/ws')

    def _publish_heartbeat(self):
        msg = String()
        msg.data = self.get_name()
        self._heartbeat_pub.publish(msg)

    def _publish_lifecycle(self, state: str):
        msg = String()
        msg.data = f'{self.get_name()}:{state}'
        self._lifecycle_pub.publish(msg)

    @_safe_callback
    def _on_flight_model_state(self, msg: FlightModelState):
        import math as _m
        data = {
            'type': 'flight_model_state',
            'lat': round(float(msg.latitude_deg), 6),
            'lon': round(float(msg.longitude_deg), 6),
            'alt_ft_msl': round(float(msg.altitude_msl_m) * 3.28084, 1),
            'alt_ft_agl': round(float(msg.altitude_agl_m) * 3.28084, 1),
            'ias_kt': round(float(msg.ias_ms) * 1.94384, 1),
            'gnd_speed_kt': round(float(msg.ground_speed_ms) * 1.94384, 1),
            'hdg_true_deg': round(float(msg.true_heading_rad) * 180.0 / _m.pi, 1),
            'track_deg': round(float(msg.ground_track_rad) * 180.0 / _m.pi, 1),
            'vs_fpm': round(float(msg.vertical_speed_ms) * 196.85, 0),
            'pitch_deg': round(float(msg.pitch_rad) * 180.0 / _m.pi, 1),
            'roll_deg': round(float(msg.roll_rad) * 180.0 / _m.pi, 1),
            'is_helicopter': bool(msg.is_helicopter),
            'cg_x_in': round(float(msg.cg_x_in), 2),
            'cg_y_in': round(float(msg.cg_y_in), 2),
            'total_mass_kg': round(float(msg.total_mass_kg), 1),
        }
        with self._lock:
            self._latest['flight_model_state'] = data

    @_safe_callback
    def _on_fuel_state(self, msg: FuelState):
        n = int(msg.tank_count) if msg.tank_count > 0 else 4
        data = {
            'type': 'fuel_state',
            'tank_count': n,
            'density_kg_per_liter': float(msg.density_kg_per_liter),
            'fuel_type': msg.fuel_type,
            'tank_quantity_kg': [round(float(msg.tank_quantity_kg[i]), 1) for i in range(n)],
            'tank_quantity_norm': [round(float(msg.tank_quantity_norm[i]), 3) for i in range(n)],
            'tank_usable_kg': [round(float(msg.tank_usable_kg[i]), 1) for i in range(n)],
            'tank_quantity_liters': [round(float(msg.tank_quantity_liters[i]), 1) for i in range(n)],
            'total_fuel_kg': round(float(msg.total_fuel_kg), 1),
            'total_fuel_norm': round(float(msg.total_fuel_norm), 3),
            'total_fuel_liters': round(float(msg.total_fuel_liters), 1),
            'engine_fuel_flow_kgs': [round(float(x), 2) for x in msg.engine_fuel_flow_kgs],
            'engine_fuel_flow_lph': [round(float(x), 1) for x in msg.engine_fuel_flow_lph],
            'fuel_pressure_pa': [round(float(msg.fuel_pressure_pa[i]), 0) for i in range(n)],
            'boost_pump_on': [bool(msg.boost_pump_on[i]) for i in range(n)],
            'tank_selected': [bool(msg.tank_selected[i]) for i in range(n)],
            'low_fuel_warning': bool(msg.low_fuel_warning),
            'cg_contribution_m': float(msg.cg_contribution_m),
        }
        with self._lock:
            self._latest['fuel_state'] = data

    @_safe_callback
    def _on_sim_state(self, msg: SimState):
        state_names = {0: 'INIT', 1: 'READY', 2: 'RUNNING', 3: 'FROZEN',
                       4: 'RESETTING', 5: 'SHUTDOWN'}
        data = {
            'type': 'sim_state',
            'state': state_names.get(msg.state, 'UNKNOWN'),
            'reposition_active': bool(msg.reposition_active),
            'freeze_position': bool(msg.freeze_position),
            'freeze_fuel': bool(msg.freeze_fuel),
            'aircraft_id': msg.aircraft_id,
            'sim_time_sec': round(float(msg.sim_time_sec), 1),
            'time_scale': round(float(msg.time_scale), 1),
        }
        with self._lock:
            self._latest['sim_state'] = data
            # Load avionics config when aircraft_id changes
            if msg.aircraft_id and msg.aircraft_id != self._loaded_aircraft_id:
                self._load_avionics_config(msg.aircraft_id)
                self._load_engine_config(msg.aircraft_id)
                self._load_fuel_config(msg.aircraft_id)
                self._load_failures_config(msg.aircraft_id)
                self._load_electrical_config(msg.aircraft_id)
                self._load_weight_config(msg.aircraft_id)

    @_safe_callback
    def _on_nav_state(self, msg: NavigationState):
        xpdr_modes = {0: 'OFF', 1: 'STBY', 2: 'ON', 3: 'ALT'}
        to_from_names = {0: 'OFF', 1: 'TO', 2: 'FROM'}
        nav_type_names = {0: 'NONE', 1: 'VOR', 2: 'VORDME', 3: 'ILS', 4: 'LOC'}
        dme_src_names = {0: 'NAV1', 1: 'HOLD', 2: 'NAV2'}
        data = {
            'type': 'nav_state',
            # GPS1
            'gps1_valid': bool(msg.gps1_valid),
            'gps1_lat_deg': round(float(msg.gps1_lat_deg), 6),
            'gps1_lon_deg': round(float(msg.gps1_lon_deg), 6),
            'gps1_alt_ft': round(float(msg.gps1_alt_ft), 0),
            'gps1_gs_kt': round(float(msg.gps1_gs_kt), 1),
            'gps1_track_deg': round(float(msg.gps1_track_deg), 1),
            # GPS2
            'gps2_valid': bool(msg.gps2_valid),
            'gps2_lat_deg': round(float(msg.gps2_lat_deg), 6),
            'gps2_lon_deg': round(float(msg.gps2_lon_deg), 6),
            'gps2_alt_ft': round(float(msg.gps2_alt_ft), 0),
            'gps2_gs_kt': round(float(msg.gps2_gs_kt), 1),
            'gps2_track_deg': round(float(msg.gps2_track_deg), 1),
            'active_gps_source': int(msg.active_gps_source),
            # NAV1
            'nav1_valid': bool(msg.nav1_valid),
            'nav1_ident': msg.nav1_ident,
            'nav1_type': nav_type_names.get(msg.nav1_type, 'NONE'),
            'nav1_obs_deg': round(float(msg.nav1_obs_deg), 1),
            'nav1_cdi_dots': round(float(msg.nav1_cdi_dots), 2),
            'nav1_bearing_deg': round(float(msg.nav1_bearing_deg), 1),
            'nav1_radial_deg': round(float(msg.nav1_radial_deg), 1),
            'nav1_distance_nm': round(float(msg.nav1_distance_nm), 1),
            'nav1_signal': round(float(msg.nav1_signal_strength), 2),
            'nav1_to_from': to_from_names.get(msg.nav1_to_from, 'OFF'),
            'nav1_gs_valid': bool(msg.nav1_gs_valid),
            'nav1_gs_dots': round(float(msg.nav1_gs_dots), 2),
            # NAV2
            'nav2_valid': bool(msg.nav2_valid),
            'nav2_ident': msg.nav2_ident,
            'nav2_type': nav_type_names.get(msg.nav2_type, 'NONE'),
            'nav2_obs_deg': round(float(msg.nav2_obs_deg), 1),
            'nav2_cdi_dots': round(float(msg.nav2_cdi_dots), 2),
            'nav2_bearing_deg': round(float(msg.nav2_bearing_deg), 1),
            'nav2_radial_deg': round(float(msg.nav2_radial_deg), 1),
            'nav2_distance_nm': round(float(msg.nav2_distance_nm), 1),
            'nav2_signal': round(float(msg.nav2_signal_strength), 2),
            'nav2_to_from': to_from_names.get(msg.nav2_to_from, 'OFF'),
            'nav2_gs_valid': bool(msg.nav2_gs_valid),
            'nav2_gs_dots': round(float(msg.nav2_gs_dots), 2),
            # ADF1
            'adf1_valid': bool(msg.adf1_valid),
            'adf1_ident': msg.adf1_ident,
            'adf1_rel_bearing_deg': round(float(msg.adf1_relative_bearing_deg), 1),
            'adf1_signal': round(float(msg.adf1_signal), 2),
            # ADF2
            'adf2_valid': bool(msg.adf2_valid),
            'adf2_ident': msg.adf2_ident,
            'adf2_rel_bearing_deg': round(float(msg.adf2_relative_bearing_deg), 1),
            'adf2_signal': round(float(msg.adf2_signal), 2),
            # DME
            'dme_source': dme_src_names.get(msg.dme_source, 'NAV1'),
            'dme_valid': bool(msg.dme_valid),
            'dme_distance_nm': round(float(msg.dme_distance_nm), 1),
            'dme_gs_kt': round(float(msg.dme_gs_kt), 1),
            # TACAN
            'tacan_valid': bool(msg.tacan_valid),
            'tacan_ident': msg.tacan_ident,
            'tacan_bearing_deg': round(float(msg.tacan_bearing_deg), 1),
            'tacan_distance_nm': round(float(msg.tacan_distance_nm), 1),
            # Markers
            'marker_outer': bool(msg.marker_outer),
            'marker_middle': bool(msg.marker_middle),
            'marker_inner': bool(msg.marker_inner),
            # Transponder
            'xpdr_code': int(msg.transponder_code),
            'xpdr_mode': xpdr_modes.get(msg.transponder_mode, 'OFF'),
            'xpdr_ident_active': bool(msg.transponder_ident_active),
            # Frequency echoes
            'com1_mhz': float(msg.com1_freq_mhz),
            'com2_mhz': float(msg.com2_freq_mhz),
            'com3_mhz': float(msg.com3_freq_mhz),
            'adf1_khz': float(msg.adf1_freq_khz),
            'adf2_khz': float(msg.adf2_freq_khz),
            # Compass heading
            'hdg_mag_deg': round(float(msg.magnetic_heading_rad) * 180.0 / math.pi, 1),
            'mag_variation_deg': round(float(msg.magnetic_variation_deg), 1),
        }
        with self._lock:
            self._latest['nav_state'] = data

    @_safe_callback
    def _on_avionics_controls(self, msg: AvionicsControls):
        xpdr_modes = {0: 'OFF', 1: 'STBY', 2: 'ON', 3: 'ALT'}
        data = {
            'type': 'avionics',
            'com1_mhz': float(msg.com1_freq_mhz),
            'com2_mhz': float(msg.com2_freq_mhz),
            'com3_mhz': float(msg.com3_freq_mhz),
            'nav1_mhz': float(msg.nav1_freq_mhz),
            'nav2_mhz': float(msg.nav2_freq_mhz),
            'adf1_khz': float(msg.adf1_freq_khz),
            'adf2_khz': float(msg.adf2_freq_khz),
            'xpdr_code': int(msg.transponder_code),
            'xpdr_mode': xpdr_modes.get(msg.transponder_mode, 'OFF'),
            'obs1_deg': float(msg.obs1_deg),
            'obs2_deg': float(msg.obs2_deg),
            'dme_source': int(msg.dme_source),
            'tacan_channel': int(msg.tacan_channel),
            'tacan_band': int(msg.tacan_band),
            'gps_source': int(msg.gps_source),
        }
        with self._lock:
            self._latest['avionics'] = data

    @_safe_callback
    def _on_electrical_state(self, msg: ElectricalState):
        data = {
            'type': 'electrical_state',
            'bus_names': list(msg.bus_names),
            'bus_voltages_v': [round(float(v), 1) for v in msg.bus_voltages_v],
            'bus_powered': [bool(p) for p in msg.bus_powered],
            'source_names': list(msg.source_names),
            'source_active': [bool(a) for a in msg.source_active],
            'source_voltages_v': [round(float(v), 1) for v in msg.source_voltages_v],
            'source_currents_a': [round(float(c), 1) for c in msg.source_currents_a],
            'load_names': list(msg.load_names),
            'load_powered': [bool(p) for p in msg.load_powered],
            'load_currents_a': [round(float(c), 1) for c in msg.load_currents_a],
            'switch_ids': list(msg.switch_ids),
            'switch_labels': list(msg.switch_labels),
            'switch_closed': [bool(c) for c in msg.switch_closed],
            'total_load_a': round(float(msg.total_load_a), 1),
            'battery_soc_pct': round(float(msg.battery_soc_pct), 1),
            'master_bus_voltage_v': round(float(msg.master_bus_voltage_v), 1),
            'avionics_bus_powered': bool(msg.avionics_bus_powered),
            'essential_bus_powered': bool(msg.essential_bus_powered),
            'cb_names': list(msg.cb_names),
            'cb_closed': [bool(c) for c in msg.cb_closed],
            'cb_tripped': [bool(t) for t in msg.cb_tripped],
        }
        with self._lock:
            self._latest['electrical_state'] = data

    @_safe_callback
    def _on_sim_alert(self, msg: SimAlert):
        severity_names = {0: 'INFO', 1: 'WARNING', 2: 'CRITICAL'}
        data = {
            'type': 'sim_alert',
            'severity': severity_names.get(msg.severity, 'UNKNOWN'),
            'source': msg.source,
            'message': msg.message,
        }
        with self._lock:
            self._latest['sim_alert'] = data
        # Also store per-node last error for tooltip display
        if msg.severity >= 1:  # WARNING or CRITICAL
            with nodes_lock:
                if msg.source in discovered_nodes:
                    discovered_nodes[msg.source]['last_error'] = msg.message
                else:
                    discovered_nodes[msg.source] = {
                        'last_seen': 0.0,
                        'lifecycle_state': 'unknown',
                        'source': 'alert',
                        'last_error': msg.message,
                    }

    @_safe_callback
    def _on_engine_state(self, msg: EngineState):
        n = msg.engine_count
        # Map engine_state enum to running/failed bools for frontend compatibility
        running = [msg.engine_state[i] == 3 for i in range(n)]  # STATE_RUNNING=3
        failed = [msg.engine_state[i] == 5 for i in range(n)]   # STATE_FAILED=5
        data = {
            'type': 'engines_state',
            'engine_count': int(n),
            'engine_type': msg.engine_type,
            'rpm': [round(float(msg.engine_rpm[i]), 0) for i in range(n)],
            'egt_degc': [round(float(msg.egt_degc[i]), 0) for i in range(n)],
            'cht_degc': [round(float(msg.cht_degc[i]), 0) for i in range(n)],
            'oil_pressure_psi': [round(float(msg.oil_press_kpa[i] * 0.145038), 1) for i in range(n)],
            'oil_temp_degc': [round(float(msg.oil_temp_degc[i]), 0) for i in range(n)],
            'manifold_pressure_inhg': [round(float(msg.manifold_press_inhg[i]), 1) for i in range(n)],
            'fuel_flow_gph': [round(float(msg.fuel_flow_kgph[i] / 2.7216), 1) for i in range(n)],
            'n1_pct': [round(float(msg.n1_pct[i]), 1) for i in range(n)],
            'n2_pct': [round(float(msg.n2_pct[i]), 1) for i in range(n)],
            'tgt_degc': [round(float(msg.tot_degc[i]), 0) for i in range(n)],
            'torque_pct': [round(float(msg.torque_pct[i]), 1) for i in range(n)],
            'engine_running': running,
            'engine_failed': failed,
            'starter_engaged': [bool(msg.starter_engaged[i]) for i in range(n)],
            'low_oil_pressure_warning': [bool(msg.low_oil_pressure_warning[i]) for i in range(n)],
            'high_egt_warning': [bool(msg.high_egt_warning[i]) for i in range(n)],
            'high_cht_warning': [bool(msg.high_cht_warning[i]) for i in range(n)],
        }
        with self._lock:
            self._latest['engines_state'] = data

    def _load_avionics_config(self, aircraft_id: str):
        """Load avionics config from aircraft package navigation.yaml."""
        try:
            from ament_index_python.packages import get_package_share_directory
            pkg = f'aircraft_{aircraft_id}'
            share_dir = get_package_share_directory(pkg)
            nav_yaml = os.path.join(share_dir, 'config', 'navigation.yaml')
            if not os.path.isfile(nav_yaml):
                self.get_logger().warn(f'No navigation.yaml for {pkg}')
                return
            with open(nav_yaml) as f:
                cfg = yaml.safe_load(f)
            avionics = cfg.get('avionics', {})
            data = {
                'type': 'avionics_config',
                'aircraft_id': aircraft_id,
                'radios': avionics.get('radios', []),
                'displays': avionics.get('displays', []),
            }
            self._latest['avionics_config'] = data
            self._loaded_aircraft_id = aircraft_id
            self.get_logger().info(
                f'Loaded avionics config for {aircraft_id}: '
                f'{len(data["radios"])} radios, {len(data["displays"])} displays')
        except Exception as e:
            self.get_logger().error(f'Failed to load avionics config: {e}')

    def _load_engine_config(self, aircraft_id: str):
        """Load engine config from aircraft package engine.yaml."""
        try:
            from ament_index_python.packages import get_package_share_directory
            pkg = f'aircraft_{aircraft_id}'
            share_dir = get_package_share_directory(pkg)
            eng_yaml = os.path.join(share_dir, 'config', 'engine.yaml')
            if not os.path.isfile(eng_yaml):
                self.get_logger().warn(f'No engine.yaml for {pkg}')
                return
            with open(eng_yaml) as f:
                cfg = yaml.safe_load(f)
            engines = cfg.get('engines', [])
            data = {
                'type': 'engine_config',
                'aircraft_id': aircraft_id,
                'engine_count': cfg.get('engine_count', 0),
                'engines': engines,
            }
            self._latest['engine_config'] = data
            self.get_logger().info(
                f'Loaded engine config for {aircraft_id}: '
                f'{data["engine_count"]} engine(s)')
        except Exception as e:
            self.get_logger().error(f'Failed to load engine config: {e}')

    def _load_fuel_config(self, aircraft_id: str):
        """Load fuel config from aircraft package fuel.yaml (v1 flat or v2 graph)."""
        try:
            from ament_index_python.packages import get_package_share_directory
            pkg = f'aircraft_{aircraft_id}'
            share_dir = get_package_share_directory(pkg)
            fuel_yaml = os.path.join(share_dir, 'config', 'fuel.yaml')
            if not os.path.isfile(fuel_yaml):
                self.get_logger().warn(f'No fuel.yaml for {pkg}')
                return
            with open(fuel_yaml) as f:
                cfg = yaml.safe_load(f)
            fuel = cfg.get('fuel', {})
            density = fuel.get('density_kg_per_liter', 0.72)

            # Detect v2 graph format: has top-level 'nodes' key
            if 'nodes' in cfg:
                data = self._parse_fuel_config_v2(cfg, fuel, density, aircraft_id)
            else:
                data = self._parse_fuel_config_v1(fuel, density, aircraft_id)

            self._latest['fuel_config'] = data
            self.get_logger().info(
                f'Loaded fuel config for {aircraft_id}: '
                f'{data["fuel_type"]}, {data["tank_count"]} tank(s), density={density}')
        except Exception as e:
            self.get_logger().error(f'Failed to load fuel config: {e}')

    def _parse_fuel_config_v1(self, fuel, density, aircraft_id):
        """Parse v1 flat fuel config (tanks + feeds + boost_pumps)."""
        tanks = fuel.get('tanks', [])
        return {
            'type': 'fuel_config',
            'format': 'v1',
            'aircraft_id': aircraft_id,
            'fuel_type': fuel.get('fuel_type', 'AVGAS_100LL'),
            'density_kg_per_liter': density,
            'display_unit': fuel.get('display_unit', 'L'),
            'tank_count': len(tanks),
            'tanks': [{
                'id': t.get('id', f'tank_{i}'),
                'name': t.get('name', t.get('id', f'Tank {i+1}')),
                'capacity_kg': t.get('capacity_kg', 0),
                'capacity_liters': round(t.get('capacity_kg', 0) / density, 1) if density > 0 else 0,
            } for i, t in enumerate(tanks)],
        }

    def _parse_fuel_config_v2(self, cfg, fuel, density, aircraft_id):
        """Parse v2 graph fuel config (nodes + connections + selectors)."""
        nodes = cfg.get('nodes', [])
        selectors = cfg.get('selectors', [])

        # Extract tank nodes
        tank_nodes = [n for n in nodes if n.get('type') == 'tank']
        pump_nodes = [n for n in nodes if n.get('type') == 'pump']

        tanks = [{
            'id': t.get('id', f'tank_{i}'),
            'name': t.get('label', t.get('id', f'Tank {i+1}')),
            'capacity_kg': t.get('capacity_kg', 0),
            'capacity_liters': round(t.get('capacity_kg', 0) / density, 1) if density > 0 else 0,
        } for i, t in enumerate(tank_nodes)]

        pumps = [{
            'id': p.get('id'),
            'label': p.get('label', p.get('id')),
            'source': p.get('source', 'engine'),
            'switch_id': p.get('switch_id', ''),
        } for p in pump_nodes]

        sel_list = [{
            'id': s.get('id'),
            'switch_id': s.get('switch_id', ''),
            'positions': s.get('positions', []),
            'default_position': s.get('default_position', ''),
        } for s in selectors]

        return {
            'type': 'fuel_config',
            'format': 'v2',
            'aircraft_id': aircraft_id,
            'fuel_type': fuel.get('fuel_type', 'AVGAS_100LL'),
            'density_kg_per_liter': density,
            'display_unit': fuel.get('display_unit', 'L'),
            'tank_count': len(tank_nodes),
            'tanks': tanks,
            'pumps': pumps,
            'selectors': sel_list,
        }

    @_safe_callback
    def _on_failure_state(self, msg: FailureState):
        data = {
            'type': 'failure_state',
            'active_failure_ids': list(msg.active_failure_ids),
            'armed_failure_ids': list(msg.armed_failure_ids),
            'armed_trigger_remaining_s': [float(t) for t in msg.armed_trigger_remaining_s],
            'failed_nav_receivers': list(msg.failed_nav_receivers),
            'failed_instruments': list(msg.failed_instruments),
        }
        with self._lock:
            self._latest['failure_state'] = data

    @_safe_callback
    def _on_terrain_source(self, msg: TerrainSource):
        source_names = {0: 'CIGI', 1: 'SRTM', 2: 'MSL', 255: 'UNKNOWN'}
        data = {
            'type': 'terrain_source',
            'source': source_names.get(msg.source, 'UNKNOWN'),
            'source_id': int(msg.source),
            'description': msg.description,
        }
        with self._lock:
            self._latest['terrain_source'] = data

    @_safe_callback
    def _on_air_data_state(self, msg: AirDataState):
        data = {
            'type': 'air_data_state',
            'ias_kt': round(msg.indicated_airspeed_ms[0] * 1.94384, 1),
            'cas_kt': round(msg.calibrated_airspeed_ms[0] * 1.94384, 1),
            'mach': round(float(msg.mach[0]), 3),
            'alt_indicated_ft': round(msg.altitude_indicated_m[0] * 3.28084, 0),
            'alt_pressure_ft': round(msg.altitude_pressure_m[0] * 3.28084, 0),
            'vs_fpm': round(msg.vertical_speed_ms[0] * 196.85, 0),
            'sat_c': round(msg.sat_k[0] - 273.15, 1),
            'tat_c': round(msg.tat_k[0] - 273.15, 1),
            'pitot_healthy': bool(msg.pitot_healthy[0]),
            'static_healthy': bool(msg.static_healthy[0]),
            'pitot_heat_on': bool(msg.pitot_heat_on[0]),
            'pitot_ice_norm': round(float(msg.pitot_ice_norm[0]), 2),
        }
        with self._lock:
            self._latest['air_data_state'] = data

    @_safe_callback
    def _on_arbitration_state(self, msg: ArbitrationState):
        src_names = {0: 'FROZEN', 1: 'HARDWARE', 2: 'VIRTUAL', 3: 'INSTRUCTOR'}
        data = {
            'type': 'arbitration_state',
            'flight_source': src_names.get(msg.flight_source, 'UNKNOWN'),
            'engine_source': src_names.get(msg.engine_source, 'UNKNOWN'),
            'avionics_source': src_names.get(msg.avionics_source, 'UNKNOWN'),
            'panel_source': src_names.get(msg.panel_source, 'UNKNOWN'),
            'hw_flight_healthy': bool(msg.hardware_flight_healthy),
            'hw_engine_healthy': bool(msg.hardware_engine_healthy),
            'hw_avionics_healthy': bool(msg.hardware_avionics_healthy),
            'hw_panel_healthy': bool(msg.hardware_panel_healthy),
            'forced_switch_ids': list(msg.forced_switch_ids),
            'forced_selector_ids': list(msg.forced_selector_ids),
        }
        with self._lock:
            self._latest['arbitration_state'] = data

    @_safe_callback
    def _on_gear_state(self, msg: GearState):
        n = min(int(msg.gear_count), 5)
        data = {
            'type': 'gear_state',
            'gear_count': int(msg.gear_count),
            'gear_type': msg.gear_type,
            'retractable': bool(msg.retractable),
            'on_ground': bool(msg.on_ground),
            'gear_handle_down': bool(msg.gear_handle_down),
            'gear_unsafe': bool(msg.gear_unsafe),
            'gear_warning': bool(msg.gear_warning),
            'leg_names': [msg.leg_names[i] for i in range(n)],
            'position_norm': [round(float(msg.position_norm[i]), 2) for i in range(n)],
            'weight_on_wheels': [bool(msg.weight_on_wheels[i]) for i in range(n)],
            'brake_left_norm': round(float(msg.brake_left_norm), 2),
            'brake_right_norm': round(float(msg.brake_right_norm), 2),
            'parking_brake': bool(msg.parking_brake),
            'nosewheel_angle_deg': round(float(msg.nosewheel_angle_deg), 1),
        }
        with self._lock:
            self._latest['gear_state'] = data

    def _on_atmosphere_state(self, msg: AtmosphereState):
        import math
        # Compute wind direction and speed from NED components for display
        wind_n = float(msg.wind_north_ms)
        wind_e = float(msg.wind_east_ms)
        wind_d = float(msg.wind_down_ms)
        wind_speed_ms = math.sqrt(wind_n ** 2 + wind_e ** 2)
        wind_speed_kt = wind_speed_ms / 0.51444
        # Wind direction is where it blows FROM — opposite of velocity vector
        if wind_speed_ms > 0.1:
            wind_dir_deg = (math.degrees(math.atan2(-wind_e, -wind_n)) + 360) % 360
        else:
            wind_dir_deg = 0.0
        # Convert units for display: K→°C, Pa→hPa
        oat_celsius = float(msg.oat_k) - 273.15
        qnh_hpa = float(msg.qnh_pa) / 100.0
        data = {
            'type': 'atmosphere',
            'qnh_hpa': round(qnh_hpa, 2),
            'oat_celsius': round(oat_celsius, 1),
            'wind_dir_deg': round(wind_dir_deg, 0),
            'wind_speed_kt': round(wind_speed_kt, 1),
            'visibility_m': 9999,  # not in AtmosphereState — default until weather authored
            'wind_north_ms': round(wind_n, 2),
            'wind_east_ms': round(wind_e, 2),
            'wind_down_ms': round(wind_d, 2),
            'visible_moisture': bool(msg.visible_moisture),
            'turbulence_intensity': round(float(msg.turbulence_intensity), 2),
        }
        with self._lock:
            self._latest['atmosphere'] = data

    def publish_weather(self, data: dict = None):
        """Publish WeatherState v2 to /world/weather.

        If `data` is provided, merge it into the cached state first (partial
        updates preserve previously-set fields). If `data` is None, republish
        from cache (used after remove_cloud_layer, station change, etc).
        """
        import math
        if data is not None:
            # dict.update replaces keys present in `data` and leaves absent keys
            # untouched. WX's ACCEPT never sends cloud_layers, so clouds are
            # preserved. WeatherPanelV2's ACCEPT owns the full cloud list and
            # sends it wholesale (possibly empty to clear all). Dedicated
            # add/remove_cloud_layer handlers still serve WX.
            self._last_weather_data.update(data)
        source = self._last_weather_data
        msg = WeatherState()
        msg.header.stamp = self.get_clock().now().to_msg()

        # Station fields preserved as empty defaults so downstream consumers
        # (IOS display only — weather_solver / cigi_bridge / plugin don't
        # read them) don't need a schema change. The V1 weather_station
        # picker was retired in Slice 5d — Global weather uses aircraft
        # ground, patches use their own ground_elevation_m.
        msg.station_icao = ''
        msg.station_elevation_m = 0.0

        # Global atmosphere (UI sends °C/hPa, convert to K/Pa)
        msg.temperature_sl_k = float(source.get('temperature_sl_k', 288.15))
        msg.pressure_sl_pa = float(source.get('pressure_sl_pa', 101325.0))
        msg.visibility_m = float(source.get('visibility_m', 9999.0))
        msg.humidity_pct = int(source.get('humidity_pct', 50))

        # Wind layers — V2 is the sole sender, always a list.
        for wl_data in source.get('wind_layers', []):
            wl = WeatherWindLayer()
            wl.altitude_msl_m      = float(wl_data.get('altitude_msl_m', 0.0))
            wl.wind_speed_ms       = float(wl_data.get('wind_speed_ms', 0.0))
            wl.wind_direction_deg  = float(wl_data.get('wind_direction_deg', 0.0))
            wl.vertical_wind_ms    = float(wl_data.get('vertical_wind_ms', 0.0))
            wl.gust_speed_ms       = float(wl_data.get('gust_speed_ms', 0.0))
            wl.shear_direction_deg = float(wl_data.get('shear_direction_deg', 0.0))
            wl.shear_speed_ms      = float(wl_data.get('shear_speed_ms', 0.0))
            wl.turbulence_severity = float(wl_data.get('turbulence_severity', 0.0))
            msg.wind_layers.append(wl)

        # Cloud layers — wire carries base_elevation_m directly (MSL).
        # Slice 5d: frontend resolves AGL→MSL at Accept time using aircraft
        # ground elevation (Global) or patch.ground_elevation_m (patches),
        # so the backend is stateless for this and no longer needs a
        # weather_station reference. Legacy base_agl_ft support kept as a
        # transitional fallback while V1 clients may still exist — can be
        # dropped in a follow-up once no legacy senders remain.
        for cl_data in source.get('cloud_layers', []):
            cl = WeatherCloudLayer()
            cl.cloud_type = int(cl_data.get('cloud_type', 0))
            cl.coverage_pct = float(cl_data.get('coverage_pct', 0.0))
            if 'base_elevation_m' in cl_data:
                cl.base_elevation_m = float(cl_data['base_elevation_m'])
            elif 'base_agl_ft' in cl_data:
                # Legacy V1 path: treat AGL as MSL (station picker retired)
                cl.base_elevation_m = float(cl_data['base_agl_ft']) * 0.3048
            else:
                cl.base_elevation_m = 0.0
            cl.thickness_m = float(cl_data.get('thickness_m', 300.0))
            cl.transition_band_m = float(cl_data.get('transition_band_m', 50.0))
            cl.scud_enable = bool(cl_data.get('scud_enable', False))
            cl.scud_frequency_pct = float(cl_data.get('scud_frequency_pct', 0.0))
            msg.cloud_layers.append(cl)

        # Precipitation — sensible defaults
        msg.precipitation_rate = float(source.get('precipitation_rate', 0.0))
        msg.precipitation_type = int(source.get('precipitation_type', 0))

        # Surface conditions
        msg.wave_height_m = 0.0
        msg.wave_direction_deg = 0.0
        msg.runway_condition_idx = int(source.get('runway_condition_idx', 0))

        # FSTD control — deterministic defaults.
        # turbulence_model defaults to MIL-F-8785C (1) so Dryden is always
        # enabled. Per-layer turbulence_severity still gates whether any
        # turbulence is actually generated — with severity=0 the Dryden
        # output is zero, so enabling the model unconditionally is safe.
        # Previous logic defaulted to 0 when no flat-triple severity was
        # set, which hid V2's per-layer turbulence authoring from the FDM.
        msg.variability_pct    = float(source.get('variability_pct', 0.0))
        msg.evolution_mode     = int(source.get('evolution_mode', 3))  # Static
        msg.deterministic_seed = int(source.get('deterministic_seed', 0))
        msg.turbulence_model   = int(source.get('turbulence_model', 1))

        # Append active microbursts
        for mb in self._active_microbursts:
            mb_msg = MicroburstHazard()
            mb_msg.hazard_id = int(mb['hazard_id'])
            mb_msg.latitude_deg = float(mb['latitude_deg'])
            mb_msg.longitude_deg = float(mb['longitude_deg'])
            mb_msg.core_radius_m = float(mb['core_radius_m'])
            mb_msg.shaft_altitude_m = float(mb['shaft_altitude_m'])
            mb_msg.intensity = float(mb['intensity'])
            mb_msg.lifecycle_phase = int(mb['lifecycle_phase'])
            mb_msg.activation_time_sec = float(mb['activation_time_sec'])
            mb_msg.source_patch_id = int(mb.get('source_patch_id', 0))
            msg.microbursts.append(mb_msg)

        # Append active patches (Slice 4 — regional weather)
        for p in self._active_patches:
            wp = WeatherPatch()
            wp.patch_id              = int(p['patch_id'])
            wp.patch_type            = p['patch_type']
            wp.label                 = p['label']
            wp.icao                  = p['icao']
            wp.lat_deg               = float(p['lat_deg'])
            wp.lon_deg               = float(p['lon_deg'])
            wp.radius_m              = float(p['radius_m'])
            wp.ground_elevation_m    = float(p['ground_elevation_m'])
            for cl_data in p.get('cloud_layers', []):
                cl = WeatherCloudLayer()
                cl.cloud_type         = int(cl_data.get('cloud_type', 0))
                cl.coverage_pct       = float(cl_data.get('coverage_pct', 0.0))
                cl.base_elevation_m   = float(cl_data.get('base_elevation_m', 0.0))
                cl.thickness_m        = float(cl_data.get('thickness_m', 300.0))
                cl.transition_band_m  = float(cl_data.get('transition_band_m', 50.0))
                cl.scud_enable        = bool(cl_data.get('scud_enable', False))
                cl.scud_frequency_pct = float(cl_data.get('scud_frequency_pct', 0.0))
                wp.cloud_layers.append(cl)
            for wl_data in p.get('wind_layers', []):
                wl = WeatherWindLayer()
                wl.altitude_msl_m     = float(wl_data.get('altitude_msl_m', 0.0))
                wl.wind_speed_ms      = float(wl_data.get('wind_speed_ms', 0.0))
                wl.wind_direction_deg = float(wl_data.get('wind_direction_deg', 0.0))
                wl.vertical_wind_ms   = float(wl_data.get('vertical_wind_ms', 0.0))
                wl.gust_speed_ms      = float(wl_data.get('gust_speed_ms', 0.0))
                wl.shear_direction_deg= float(wl_data.get('shear_direction_deg', 0.0))
                wl.shear_speed_ms     = float(wl_data.get('shear_speed_ms', 0.0))
                wl.turbulence_severity= float(wl_data.get('turbulence_severity', 0.0))
                wp.wind_layers.append(wl)
            wp.override_visibility    = bool(p['override_visibility'])
            wp.visibility_m           = float(p['visibility_m'])
            wp.override_temperature   = bool(p['override_temperature'])
            wp.temperature_k          = float(p['temperature_k'])
            wp.override_precipitation = bool(p['override_precipitation'])
            wp.precipitation_rate     = float(p['precipitation_rate'])
            wp.precipitation_type     = int(p['precipitation_type'])
            wp.override_humidity      = bool(p.get('override_humidity', False))
            wp.humidity_pct           = int(p.get('humidity_pct', 50))
            wp.override_pressure      = bool(p.get('override_pressure', False))
            wp.pressure_sl_pa         = float(p.get('pressure_sl_pa', 101325.0))
            wp.override_runway        = bool(p.get('override_runway', False))
            wp.runway_condition_idx   = int(p.get('runway_condition_idx', 0))
            msg.patches.append(wp)

        self._weather_pub.publish(msg)
        self.get_logger().info(
            f'Published WeatherState v2: station={msg.station_icao or "none"} '
            f'T={msg.temperature_sl_k:.1f}K P={msg.pressure_sl_pa:.0f}Pa '
            f'vis={msg.visibility_m:.0f}m clouds={len(msg.cloud_layers)} '
            f'wind_layers={len(msg.wind_layers)} microbursts={len(msg.microbursts)}')
        self._broadcast_weather_state()

    def activate_microburst(self, data: dict):
        """Compute lat/lon from bearing/distance relative to aircraft position."""
        import math
        with self._lock:
            fms = self._latest.get('flight_model_state', {})
            sim = self._latest.get('sim_state', {})

        # Reference point is always the aircraft now that the weather_station
        # picker is retired (Slice 5d). Slice 5c will rework microburst
        # authoring into the patch-radius model — at which point the IOS
        # can author microbursts by absolute lat/lon via the map.
        ref_lat = fms.get('lat', 0.0)
        ref_lon = fms.get('lon', 0.0)
        ref_label = 'aircraft'

        bearing_deg = float(data.get('bearing_deg', 0))
        distance_nm = float(data.get('distance_nm', 3))
        bearing_rad = math.radians(bearing_deg)
        ref_lat_rad = math.radians(ref_lat)

        # Compute microburst lat/lon from reference position + bearing + distance
        mb_lat = ref_lat + (distance_nm / 60.0) * math.cos(bearing_rad)
        mb_lon = ref_lon + (distance_nm / 60.0) * math.sin(bearing_rad) / math.cos(ref_lat_rad)

        mb = {
            'hazard_id': self._mb_next_id,
            'latitude_deg': mb_lat,
            'longitude_deg': mb_lon,
            'core_radius_m': float(data.get('core_radius_m', 1000)),
            'shaft_altitude_m': float(data.get('shaft_altitude_m', 300)),
            'intensity': float(data.get('intensity', 10.0)),
            'lifecycle_phase': 2,  # Mature
            'activation_time_sec': float(sim.get('sim_time_sec', 0)),
        }
        self._mb_next_id += 1
        self._active_microbursts.append(mb)

        self.get_logger().info(
            f'Microburst #{mb["hazard_id"]} activated: lat={mb_lat:.5f} lon={mb_lon:.5f} '
            f'R={mb["core_radius_m"]:.0f}m lambda={mb["intensity"]:.1f} ref={ref_label}')

        # Republish weather with updated microbursts
        self.publish_weather()
        self._broadcast_microbursts()

    def clear_microburst(self, hazard_id: int):
        """Remove a specific microburst by ID."""
        self._active_microbursts = [
            mb for mb in self._active_microbursts if mb['hazard_id'] != hazard_id
        ]
        self.get_logger().info(f'Microburst #{hazard_id} cleared')
        self.publish_weather()
        self._broadcast_microbursts()

    def clear_all_microbursts(self):
        """Remove all active microbursts."""
        self._active_microbursts.clear()
        self.get_logger().info('All microbursts cleared')
        self.publish_weather()
        self._broadcast_microbursts()

    def set_patch_microburst(self, data: dict):
        """Create or update a microburst tied to a patch.

        Wire payload: patch_id (uint16, must match existing patch),
        latitude_deg, longitude_deg (client-resolved), core_radius_m,
        intensity, shaft_altitude_m.

        At most ONE microburst per patch — updates in-place if one already
        exists with matching source_patch_id.
        """
        patch_id = int(data.get('patch_id', 0))
        if patch_id == 0:
            self.get_logger().warn('set_patch_microburst: patch_id required')
            return
        if not any(p['patch_id'] == patch_id for p in self._active_patches):
            self.get_logger().warn(f'set_patch_microburst: unknown patch_id {patch_id}')
            return

        existing = next((mb for mb in self._active_microbursts
                         if mb.get('source_patch_id') == patch_id), None)

        with self._lock:
            sim_time = self._latest.get('sim_state', {}).get('sim_time_sec', 0.0)

        if existing:
            existing['latitude_deg']     = float(data['latitude_deg'])
            existing['longitude_deg']    = float(data['longitude_deg'])
            existing['core_radius_m']    = float(data.get('core_radius_m', 1000.0))
            existing['intensity']        = float(data.get('intensity', 10.0))
            existing['shaft_altitude_m'] = float(data.get('shaft_altitude_m', 300.0))
            self.get_logger().info(
                f'Microburst #{existing["hazard_id"]} updated (patch_id={patch_id})')
        else:
            mb = {
                'hazard_id':           self._mb_next_id,
                'source_patch_id':     patch_id,
                'latitude_deg':        float(data['latitude_deg']),
                'longitude_deg':       float(data['longitude_deg']),
                'core_radius_m':       float(data.get('core_radius_m', 1000.0)),
                'intensity':           float(data.get('intensity', 10.0)),
                'shaft_altitude_m':    float(data.get('shaft_altitude_m', 300.0)),
                'lifecycle_phase':     2,  # Mature
                'activation_time_sec': float(sim_time),
            }
            self._mb_next_id += 1
            self._active_microbursts.append(mb)
            self.get_logger().info(
                f'Microburst #{mb["hazard_id"]} created (patch_id={patch_id})')

        self.publish_weather()
        self._broadcast_microbursts()

    def clear_patch_microburst(self, data: dict):
        """Remove the microburst tied to a given patch."""
        patch_id = int(data.get('patch_id', 0))
        if patch_id == 0:
            return
        before = len(self._active_microbursts)
        self._active_microbursts = [
            mb for mb in self._active_microbursts
            if mb.get('source_patch_id') != patch_id
        ]
        if len(self._active_microbursts) < before:
            self.get_logger().info(f'Microburst cleared (patch_id={patch_id})')
            self.publish_weather()
            self._broadcast_microbursts()

    def _broadcast_microbursts(self):
        """Send current microburst list to all WS clients."""
        data = {
            'type': 'microbursts',
            'hazards': [
                {
                    'hazard_id': mb['hazard_id'],
                    'source_patch_id': mb.get('source_patch_id', 0),
                    'core_radius_m': mb['core_radius_m'],
                    'intensity': mb['intensity'],
                    'shaft_altitude_m': mb.get('shaft_altitude_m', 300.0),
                    'latitude_deg': mb['latitude_deg'],
                    'longitude_deg': mb['longitude_deg'],
                }
                for mb in self._active_microbursts
            ],
        }
        with self._lock:
            self._latest['microbursts'] = data

    def _broadcast_weather_state(self):
        """Send full cached weather state (IOS-friendly units) to all WS clients."""
        source = self._last_weather_data or {}

        # Cloud layers — Slice 5d: broadcast MSL directly. Frontend resolves
        # MSL→AGL for display using current aircraft ground (Global tab) or
        # the patch's own ground_elevation_m (patch tabs). Symmetric with
        # the Accept path; backend is stateless for the AGL↔MSL conversion.
        cloud_layers = []
        for cl in source.get('cloud_layers', []):
            # Prefer explicit base_elevation_m; fall back to base_agl_ft
            # only for legacy V1 cache entries (treated as MSL since the
            # station picker is gone).
            if 'base_elevation_m' in cl:
                base_m = float(cl['base_elevation_m'])
            elif 'base_agl_ft' in cl:
                base_m = float(cl['base_agl_ft']) * 0.3048
            else:
                base_m = 0.0
            cloud_layers.append({
                'cloud_type': int(cl.get('cloud_type', 0)),
                'base_elevation_m': base_m,
                'thickness_m': float(cl.get('thickness_m', 0)),
                'coverage_pct': float(cl.get('coverage_pct', 0)),
            })

        # Wind. V2 Accept path stores a `wind_layers` list; V1 Accept path
        # stores the flat triple. Broadcast the full 8-field shape so the
        # frontend doesn't round-trip-lose vertical/gust/shear.
        wind_layers = []
        src_wind = source.get('wind_layers')
        if isinstance(src_wind, list):
            for wl in src_wind:
                wind_layers.append({
                    'altitude_msl_m':      float(wl.get('altitude_msl_m', 0.0)),
                    'wind_direction_deg':  float(wl.get('wind_direction_deg', 0.0)),
                    'wind_speed_ms':       float(wl.get('wind_speed_ms', 0.0)),
                    'vertical_wind_ms':    float(wl.get('vertical_wind_ms', 0.0)),
                    'gust_speed_ms':       float(wl.get('gust_speed_ms', 0.0)),
                    'shear_direction_deg': float(wl.get('shear_direction_deg', 0.0)),
                    'shear_speed_ms':      float(wl.get('shear_speed_ms', 0.0)),
                    'turbulence_severity': float(wl.get('turbulence_severity', 0.0)),
                })
        else:
            wd = source.get('wind_direction_deg')
            ws = source.get('wind_speed_ms')
            turb = source.get('turbulence_severity')
            if wd is not None or ws is not None or turb is not None:
                wind_layers.append({
                    'altitude_msl_m':      0.0,
                    'wind_direction_deg':  float(wd or 0.0),
                    'wind_speed_ms':       float(ws or 0.0),
                    'vertical_wind_ms':    0.0,
                    'gust_speed_ms':       float(source.get('gust_speed_ms', 0.0)),
                    'shear_direction_deg': 0.0,
                    'shear_speed_ms':      0.0,
                    'turbulence_severity': float(turb or 0.0),
                })

        data = {
            'type': 'weather_state',
            'data': {
                # Station picker retired in Slice 5d; fields preserved
                # as empty defaults for schema compatibility only.
                'station_icao': '',
                'station_elevation_m': 0.0,
                'visibility_m': float(source.get('visibility_m', 9999.0)),
                'temperature_sl_k': float(source.get('temperature_sl_k', 288.15)),
                'pressure_sl_pa': float(source.get('pressure_sl_pa', 101325.0)),
                'humidity_pct': int(source.get('humidity_pct', 50)),
                'cloud_layers': cloud_layers,
                'wind_layers': wind_layers,
                'precipitation_rate': float(source.get('precipitation_rate', 0.0)),
                'precipitation_type': int(source.get('precipitation_type', 0)),
                'runway_condition_idx': int(source.get('runway_condition_idx', 0)),
            }
        }
        with self._lock:
            self._latest['weather_state'] = data

    # ── WeatherPatch lifecycle (Slice 5c-refactor-I) ─────────────────────
    # Separates identity commits (airport/coords/radius/ground) from
    # override commits (per-field flags + layers). patch_id is assigned
    # synchronously on reserve so the user doesn't wait on SRTM/airport
    # DB before the tab is interactive; ground_elevation_m converges in
    # the background via a follow-up update_patch_identity. Async
    # wrappers in the WS handler section drive SearchAirports / SRTM
    # service calls.

    def reserve_patch(self, data: dict) -> dict:
        """Sync: allocate patch_id, append patch with identity + disabled
        overrides. Called first on any patch creation. Caller (usually
        _handle_reserve_patch) may follow up with async SRTM/SearchAirports
        resolution + update_patch_identity.

        Raises ValueError on validation failure or cap breach.
        """
        if len(self._active_patches) >= 10:
            raise ValueError('max 10 patches per session')

        _validate_identity(data, require_all=True)

        ptype = data['patch_type']
        patch = {
            'patch_id':              self._next_patch_id,
            'client_id':             str(data.get('client_id', '')),
            'patch_type':            ptype,
            'role':                  data.get('role', 'custom'),
            'label':                 data.get('label', ''),
            'icao':                  data.get('icao', ''),
            'lat_deg':               float(data['lat_deg']),
            'lon_deg':               float(data['lon_deg']),
            'radius_m':              float(data['radius_m']),
            'ground_elevation_m':    float(data.get('ground_elevation_m', 0.0)),
            # Overrides start disabled — reserved patch is FDM-invisible.
            'cloud_layers':          [],
            'wind_layers':           [],
            'override_visibility':   False, 'visibility_m':       10000.0,
            'override_temperature':  False, 'temperature_k':      288.15,
            'override_precipitation':False, 'precipitation_rate': 0.0,
                                            'precipitation_type': 0,
            'override_humidity':     False, 'humidity_pct':       50,
            'override_pressure':     False, 'pressure_sl_pa':     101325.0,
            'override_runway':       False, 'runway_condition_idx': 0,
        }
        self._next_patch_id += 1
        self._active_patches.append(patch)
        self.get_logger().info(
            f'[patch] reserved id={patch["patch_id"]} type={ptype} '
            f'icao={patch["icao"]!r} label={patch["label"]!r} '
            f'client_id={patch["client_id"]!r}'
        )
        self.publish_weather()
        self._broadcast_patches()
        return patch

    def update_patch_identity(self, data: dict) -> dict:
        """Sync: apply identity fields (icao, label, lat/lon, radius,
        ground_elevation_m, patch_type) to an existing patch. Override
        fields and layers are preserved. patch_id is required.

        Raises ValueError if patch_id missing/unknown or validation fails.
        """
        patch_id = data.get('patch_id')
        if patch_id is None:
            raise ValueError('update_patch_identity requires patch_id')
        patch = next((p for p in self._active_patches
                      if p['patch_id'] == int(patch_id)), None)
        if patch is None:
            raise ValueError(f'patch_id={patch_id} not found')

        _validate_identity(data, require_all=False)

        for f in ('patch_type', 'role', 'label', 'icao'):
            if f in data:
                patch[f] = str(data[f]) if f != 'role' else data[f]
        for f in ('lat_deg', 'lon_deg', 'radius_m', 'ground_elevation_m'):
            if f in data:
                patch[f] = float(data[f])

        self.get_logger().info(
            f'[patch] identity updated id={patch_id} '
            f'icao={patch["icao"]!r} ground_m={patch["ground_elevation_m"]:.1f}')
        self.publish_weather()
        self._broadcast_patches()
        return patch

    def update_patch_overrides(self, data: dict) -> dict:
        """Sync: apply override flags/values and layers to an existing
        patch. Identity fields are preserved. patch_id is required.

        Raises ValueError if patch_id missing/unknown or validation fails.
        """
        patch_id = data.get('patch_id')
        if patch_id is None:
            raise ValueError('update_patch_overrides requires patch_id')
        patch = next((p for p in self._active_patches
                      if p['patch_id'] == int(patch_id)), None)
        if patch is None:
            raise ValueError(f'patch_id={patch_id} not found')

        _validate_overrides(data)

        OVERRIDE_FIELDS = (
            'override_visibility',    'visibility_m',
            'override_temperature',   'temperature_k',
            'override_precipitation', 'precipitation_rate', 'precipitation_type',
            'override_humidity',      'humidity_pct',
            'override_pressure',      'pressure_sl_pa',
            'override_runway',        'runway_condition_idx',
        )
        for f in OVERRIDE_FIELDS:
            if f in data:
                patch[f] = data[f]
        if 'cloud_layers' in data:
            patch['cloud_layers'] = data['cloud_layers']
        if 'wind_layers' in data:
            patch['wind_layers'] = data['wind_layers']

        self.get_logger().info(f'[patch] overrides updated id={patch_id}')
        self.publish_weather()
        self._broadcast_patches()
        return patch

    def remove_patch(self, patch_id: int) -> bool:
        """Remove a patch by patch_id. Returns True on success, False if not
        found. Idempotent — safe to call with unknown id.
        """
        for i, p in enumerate(self._active_patches):
            if p['patch_id'] == patch_id:
                self._active_patches.pop(i)
                self.get_logger().info(f'[patch] removed id={patch_id}')
                # Cascade-delete any microburst tied to this patch (Slice 5c)
                before = len(self._active_microbursts)
                self._active_microbursts = [
                    mb for mb in self._active_microbursts
                    if mb.get('source_patch_id') != patch_id
                ]
                cascaded = before > len(self._active_microbursts)
                if cascaded:
                    self.get_logger().info(
                        f'[patch] cascade-deleted microburst for patch_id={patch_id}')
                self.publish_weather()
                self._broadcast_patches()
                if cascaded:
                    self._broadcast_microbursts()
                return True
        return False

    def clear_patches(self) -> None:
        """Remove all patches."""
        self._active_patches.clear()
        self.get_logger().info('[patch] cleared all')
        self.publish_weather()
        self._broadcast_patches()

    def _broadcast_patches(self):
        """Send current patch list to all WS clients via _latest snapshot."""
        data = {
            'type': 'patches',
            'patches': [
                {
                    'patch_id':              p['patch_id'],
                    'client_id':             p.get('client_id', ''),
                    'patch_type':            p['patch_type'],
                    'role':                  p.get('role', 'custom'),
                    'label':                 p['label'],
                    'icao':                  p['icao'],
                    'lat_deg':               p['lat_deg'],
                    'lon_deg':               p['lon_deg'],
                    'radius_m':              p['radius_m'],
                    'ground_elevation_m':    p['ground_elevation_m'],
                    'cloud_layers':          p['cloud_layers'],
                    'wind_layers':           p['wind_layers'],
                    'override_visibility':   p['override_visibility'],
                    'visibility_m':          p['visibility_m'],
                    'override_temperature':  p['override_temperature'],
                    'temperature_k':         p['temperature_k'],
                    'override_precipitation':p['override_precipitation'],
                    'precipitation_rate':    p['precipitation_rate'],
                    'precipitation_type':    p['precipitation_type'],
                    'override_humidity':     p['override_humidity'],
                    'humidity_pct':          p['humidity_pct'],
                    'override_pressure':     p['override_pressure'],
                    'pressure_sl_pa':        p['pressure_sl_pa'],
                    'override_runway':       p['override_runway'],
                    'runway_condition_idx':  p['runway_condition_idx'],
                }
                for p in self._active_patches
            ],
        }
        with self._lock:
            self._latest['patches'] = data

    def _load_failures_config(self, aircraft_id: str):
        """Load failure catalog from aircraft package failures.yaml."""
        try:
            from ament_index_python.packages import get_package_share_directory
            pkg = f'aircraft_{aircraft_id}'
            share_dir = get_package_share_directory(pkg)
            yaml_path = os.path.join(share_dir, 'config', 'failures.yaml')
        except Exception:
            yaml_path = None

        # Fallback to source tree
        if not yaml_path or not os.path.isfile(yaml_path):
            yaml_path = f'src/aircraft/{aircraft_id}/config/failures.yaml'

        if not os.path.isfile(yaml_path):
            self.get_logger().warn(f'No failures.yaml for {aircraft_id}')
            return

        try:
            with open(yaml_path) as f:
                cfg = yaml.safe_load(f)
            failures = cfg.get('failures', [])
            catalog = []
            for entry in failures:
                item = {
                    'id': entry['id'],
                    'display_name': entry.get('display_name', entry['id']),
                    'category': entry.get('category', ''),
                    'ata': entry.get('ata'),
                    'handler': entry.get('injection', {}).get('handler', ''),
                    'method': entry.get('injection', {}).get('method', ''),
                    'params': entry.get('injection', {}).get('params', {}),
                }
                catalog.append(item)
            data = {
                'type': 'failures_config',
                'aircraft_id': aircraft_id,
                'catalog': catalog,
            }
            with self._lock:
                self._latest['failures_config'] = data
            self.get_logger().info(
                f'Loaded failures config for {aircraft_id}: {len(catalog)} failures')
        except Exception as e:
            self.get_logger().error(f'Failed to load failures config: {e}')

    def _load_electrical_config(self, aircraft_id: str):
        """Load electrical topology from aircraft package electrical.yaml."""
        try:
            from ament_index_python.packages import get_package_share_directory
            pkg = f'aircraft_{aircraft_id}'
            share_dir = get_package_share_directory(pkg)
            yaml_path = os.path.join(share_dir, 'config', 'electrical.yaml')
        except Exception:
            yaml_path = None

        # Fallback to source tree
        if not yaml_path or not os.path.isfile(yaml_path):
            yaml_path = f'src/aircraft/{aircraft_id}/config/electrical.yaml'

        if not os.path.isfile(yaml_path):
            self.get_logger().warn(f'No electrical.yaml for {aircraft_id}')
            return

        try:
            with open(yaml_path) as f:
                cfg = yaml.safe_load(f)

            if 'nodes' in cfg:
                # v2 graph format: nodes[] + connections[]
                data = self._parse_electrical_v2(cfg, aircraft_id)
            else:
                # v1 flat format: sources/buses/switches/loads
                data = {
                    'type': 'electrical_config',
                    'aircraft_id': aircraft_id,
                    'sources': cfg.get('sources', []),
                    'buses': cfg.get('buses', []),
                    'switches': cfg.get('switches', []),
                    'loads': cfg.get('loads', []),
                    'direct_connections': cfg.get('direct_connections', []),
                }

            with self._lock:
                self._latest['electrical_config'] = data
            self.get_logger().info(
                f'Loaded electrical config for {aircraft_id}: '
                f'{len(data["sources"])} sources, {len(data["buses"])} buses, '
                f'{len(data["switches"])} switches, {len(data["loads"])} loads')
        except Exception as e:
            self.get_logger().error(f'Failed to load electrical config: {e}')

    def _parse_electrical_v2(self, cfg, aircraft_id):
        """Parse v2 graph topology (nodes + connections) into frontend format."""
        nodes = cfg.get('nodes', [])
        connections = cfg.get('connections', [])

        sources = [n for n in nodes if n.get('type') == 'source']
        buses = [n for n in nodes if n.get('type') == 'bus']
        loads = [n for n in nodes if n.get('type') == 'load']

        sw_types = {'switch', 'contactor', 'relay'}
        switches = [
            c for c in connections
            if c.get('type') in sw_types and c.get('pilot_controllable', True)
        ]
        cbs = [
            c for c in connections
            if c.get('type') == 'circuit_breaker' and c.get('pilot_controllable', True)
        ]

        # Build load_id → gating switch id mapping from connections
        switch_by_to = {}
        for c in connections:
            if c.get('type') in sw_types:
                switch_by_to[c['to']] = c['id']

        # Attach switch_id to loads (frontend uses this for load switch rendering)
        for ld in loads:
            ld['switch_id'] = switch_by_to.get(ld['id'], '')

        return {
            'type': 'electrical_config',
            'aircraft_id': aircraft_id,
            'sources': sources,
            'buses': buses,
            'switches': switches,
            'loads': loads,
            'cbs': cbs,
        }

    def _load_weight_config(self, aircraft_id: str):
        """Load weight & balance config from aircraft package weight.yaml."""
        try:
            from ament_index_python.packages import get_package_share_directory
            pkg = f'aircraft_{aircraft_id}'
            share_dir = get_package_share_directory(pkg)
            weight_yaml = os.path.join(share_dir, 'config', 'weight.yaml')
            if not os.path.isfile(weight_yaml):
                self.get_logger().info(f'No weight.yaml for {pkg} — W&B panel disabled')
                return
            with open(weight_yaml) as f:
                cfg = yaml.safe_load(f)
            data = {
                'type': 'weight_config',
                'aircraft_id': aircraft_id,
                **cfg,
            }
            self._latest['weight_config'] = data
            stations = len(cfg.get('payload_stations', []))
            tanks = len(cfg.get('fuel_stations', []))
            self.get_logger().info(
                f'Loaded weight config for {aircraft_id}: '
                f'{stations} payload stations, {tanks} fuel stations')
            # Publish default payload weights to JSBSim on load
            payload_msg = PayloadCommand()
            payload_msg.header.stamp = self.get_clock().now().to_msg()
            for s in cfg.get('payload_stations', []):
                payload_msg.station_indices.append(int(s['index']))
                payload_msg.weights_lbs.append(float(s.get('default_lbs', 0)))
            self._payload_pub.publish(payload_msg)
        except Exception as e:
            self.get_logger().error(f'Failed to load weight config: {e}')

    def publish_failure_command(self, data: dict):
        """Publish a FailureCommand to /aircraft/devices/instructor/failure_command."""
        msg = FailureCommand()
        msg.action = data.get('action', 'inject')
        msg.failure_id = data.get('failure_id', '')
        msg.trigger_mode = data.get('trigger_mode', '')
        msg.trigger_delay_s = float(data.get('trigger_delay_s', 0.0))
        msg.condition_param = data.get('condition_param', '')
        msg.condition_operator = data.get('condition_operator', '')
        msg.condition_value = float(data.get('condition_value', 0.0))
        msg.condition_duration_s = float(data.get('condition_duration_s', 0.0))
        msg.params_override_json = data.get('params_override_json', '')
        self._failure_cmd_pub.publish(msg)
        self.get_logger().info(
            f'Published failure command: {msg.action} {msg.failure_id}')

    def publish_avionics(self, data: dict):
        """Publish RawAvionicsControls to /aircraft/devices/instructor/controls/avionics."""
        msg = RawAvionicsControls()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.com1_freq_mhz = float(data.get('com1_mhz', 118.10))
        msg.com2_freq_mhz = float(data.get('com2_mhz', 121.50))
        msg.com3_freq_mhz = float(data.get('com3_mhz', 0.0))
        msg.nav1_freq_mhz = float(data.get('nav1_mhz', 109.10))
        msg.nav2_freq_mhz = float(data.get('nav2_mhz', 110.30))
        msg.obs1_deg = float(data.get('obs1_deg', 0.0))
        msg.obs2_deg = float(data.get('obs2_deg', 0.0))
        msg.adf1_freq_khz = float(data.get('adf1_khz', 0.0))
        msg.adf2_freq_khz = float(data.get('adf2_khz', 0.0))
        msg.transponder_code = int(data.get('xpdr_code', 2000))
        msg.transponder_mode = int(data.get('xpdr_mode', 0))
        msg.dme_source = int(data.get('dme_source', 0))
        msg.tacan_channel = int(data.get('tacan_channel', 0))
        msg.tacan_band = int(data.get('tacan_band', 0))
        msg.gps_source = int(data.get('gps_source', 0))
        self._raw_avionics_pub.publish(msg)
        self.get_logger().debug(
            f'Published instructor avionics: NAV1={msg.nav1_freq_mhz} NAV2={msg.nav2_freq_mhz}')

    def publish_virtual_avionics(self, data: dict):
        """Publish RawAvionicsControls to /aircraft/devices/virtual/controls/avionics."""
        msg = RawAvionicsControls()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.com1_freq_mhz = float(data.get('com1_mhz', 118.10))
        msg.com2_freq_mhz = float(data.get('com2_mhz', 121.50))
        msg.com3_freq_mhz = float(data.get('com3_mhz', 0.0))
        msg.nav1_freq_mhz = float(data.get('nav1_mhz', 109.10))
        msg.nav2_freq_mhz = float(data.get('nav2_mhz', 110.30))
        msg.obs1_deg = float(data.get('obs1_deg', 0.0))
        msg.obs2_deg = float(data.get('obs2_deg', 0.0))
        msg.adf1_freq_khz = float(data.get('adf1_khz', 0.0))
        msg.adf2_freq_khz = float(data.get('adf2_khz', 0.0))
        msg.transponder_code = int(data.get('xpdr_code', 2000))
        msg.transponder_mode = int(data.get('xpdr_mode', 0))
        msg.dme_source = int(data.get('dme_source', 0))
        msg.tacan_channel = int(data.get('tacan_channel', 0))
        msg.tacan_band = int(data.get('tacan_band', 0))
        msg.gps_source = int(data.get('gps_source', 0))
        self._virtual_avionics_pub.publish(msg)
        self.get_logger().debug(
            f'Published virtual avionics: COM1={msg.com1_freq_mhz} NAV1={msg.nav1_freq_mhz}')

    def _build_raw_flight_msg(self, data: dict) -> RawFlightControls:
        msg = RawFlightControls()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.aileron_norm       = float(data.get('aileron_norm', 0.0))
        msg.elevator_norm      = float(data.get('elevator_norm', 0.0))
        msg.rudder_norm        = float(data.get('rudder_norm', 0.0))
        msg.collective_norm    = float(data.get('collective_norm', 0.0))
        msg.trim_aileron_norm  = float(data.get('trim_aileron_norm', 0.0))
        msg.trim_elevator_norm = float(data.get('trim_elevator_norm', 0.0))
        msg.trim_rudder_norm   = float(data.get('trim_rudder_norm', 0.0))
        msg.brake_left_norm    = float(data.get('brake_left_norm', 0.0))
        msg.brake_right_norm   = float(data.get('brake_right_norm', 0.0))
        msg.parking_brake      = bool(data.get('parking_brake', False))
        return msg

    def _build_raw_engine_msg(self, data: dict) -> RawEngineControls:
        msg = RawEngineControls()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.throttle_norm  = [float(t) for t in data.get('throttle_norm', [0.0])]
        msg.mixture_norm   = [float(m) for m in data.get('mixture_norm', [1.0])]
        msg.condition_norm = [float(c) for c in data.get('condition_norm', [])]
        msg.prop_lever_norm = [float(r) for r in data.get('prop_lever_norm', [])]
        msg.magneto_left  = [bool(m) for m in data.get('magneto_left', [])]
        msg.magneto_right = [bool(m) for m in data.get('magneto_right', [])]
        msg.starter = bool(data.get('starter', False))
        return msg

    def publish_flight_controls(self, data: dict):
        """Publish RawFlightControls to /aircraft/devices/instructor/controls/flight."""
        self._raw_flight_pub.publish(self._build_raw_flight_msg(data))

    def publish_virtual_flight_controls(self, data: dict):
        """Publish RawFlightControls to /aircraft/devices/virtual/controls/flight."""
        self._raw_flight_virt_pub.publish(self._build_raw_flight_msg(data))

    def publish_engine_controls(self, data: dict):
        """Publish RawEngineControls to /aircraft/devices/instructor/controls/engine."""
        self._raw_engine_pub.publish(self._build_raw_engine_msg(data))

    def publish_virtual_engine_controls(self, data: dict):
        """Publish RawEngineControls to /aircraft/devices/virtual/controls/engine."""
        self._raw_engine_virt_pub.publish(self._build_raw_engine_msg(data))

    @_safe_callback
    def _on_heartbeat(self, msg: String):
        node_name = msg.data
        with nodes_lock:
            if node_name not in discovered_nodes:
                discovered_nodes[node_name] = {'lifecycle_state': 'active', 'source': 'heartbeat'}
            else:
                # If lifecycle_state is still unknown, infer active from heartbeat
                if discovered_nodes[node_name].get('lifecycle_state', 'unknown') == 'unknown':
                    discovered_nodes[node_name]['lifecycle_state'] = 'active'
            discovered_nodes[node_name]['last_seen'] = time.time()

    @_safe_callback
    def _on_lifecycle_state(self, msg: String):
        parts = msg.data.split(':', 1)
        if len(parts) == 2:
            node_name, state = parts
            with nodes_lock:
                if node_name not in discovered_nodes:
                    discovered_nodes[node_name] = {'last_seen': 0.0, 'source': 'lifecycle'}
                discovered_nodes[node_name]['lifecycle_state'] = state
                # Clear last error on successful configure (inactive) or activate (active)
                if state in ('inactive', 'active'):
                    discovered_nodes[node_name].pop('last_error', None)

    def get_node_names(self):
        """Query the ROS2 graph for all live node names."""
        try:
            names_and_ns = self.get_node_names_and_namespaces()
            return [name for name, _ns in names_and_ns]
        except Exception:
            return []

    def get_snapshot(self):
        """Return copy of latest state for all topics."""
        with self._lock:
            return dict(self._latest)

    def publish_panel(self, data: dict):
        msg = PanelControls()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.switch_ids = data.get('switch_ids', [])
        msg.switch_states = data.get('switch_states', [])
        msg.switch_forced = data.get('switch_forced', [])
        msg.selector_ids = data.get('selector_ids', [])
        msg.selector_values = data.get('selector_values', [])
        msg.selector_forced = data.get('selector_forced', [])
        msg.pot_ids = data.get('pot_ids', [])
        msg.pot_values = data.get('pot_values', [])
        msg.encoder_abs_ids = data.get('encoder_abs_ids', [])
        msg.encoder_abs_values = data.get('encoder_abs_values', [])
        msg.encoder_rel_ids = data.get('encoder_rel_ids', [])
        msg.encoder_rel_deltas = data.get('encoder_rel_deltas', [])
        self._panel_pub.publish(msg)

    def publish_virtual_panel(self, data: dict):
        """Publish PanelControls to /aircraft/devices/virtual/panel (cockpit pages)."""
        msg = PanelControls()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.switch_ids = data.get('switch_ids', [])
        msg.switch_states = data.get('switch_states', [])
        msg.selector_ids = data.get('selector_ids', [])
        msg.selector_values = data.get('selector_values', [])
        msg.pot_ids = data.get('pot_ids', [])
        msg.pot_values = data.get('pot_values', [])
        msg.encoder_abs_ids = data.get('encoder_abs_ids', [])
        msg.encoder_abs_values = data.get('encoder_abs_values', [])
        msg.encoder_rel_ids = data.get('encoder_rel_ids', [])
        msg.encoder_rel_deltas = data.get('encoder_rel_deltas', [])
        self._virtual_panel_pub.publish(msg)

    def publish_command(self, cmd_id: int, payload: dict = None):
        """Publish a SimCommand to /sim/command."""
        ros_cmd = self._cmd_map.get(cmd_id)
        if ros_cmd is None:
            self.get_logger().warn(f'Unknown command ID: {cmd_id}')
            return
        msg = SimCommand()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.command = ros_cmd
        msg.payload_json = json.dumps(payload) if payload else ''
        self._cmd_pub.publish(msg)
        self.get_logger().info(f'Published command: {cmd_id} -> ROS2 cmd {ros_cmd}')


# ── Globals ───────────────────────────────────────────────────────────────────

ros_node: IosBackendNode = None
app = FastAPI()
connected_clients = set()
stub_sim_state = 'READY'
stub_sim_time = 0.0

# { node_name: { 'last_seen': float, 'lifecycle_state': str, 'in_graph': bool } }
discovered_nodes = {}
nodes_lock = threading.Lock()


async def broadcast(data):
    """Send a message to all connected WebSocket clients."""
    global connected_clients
    msg = _dumps(data)
    disconnected = set()
    for client in connected_clients:
        try:
            await client.send_text(msg)
        except Exception:
            disconnected.add(client)
    connected_clients -= disconnected


# ── Node lifecycle helpers ────────────────────────────────────────────────────

async def call_lifecycle(node_name, transition_id):
    """Call lifecycle service for a node. Returns True on success."""
    if not ros_node:
        raise RuntimeError('ROS2 node not initialized')
    service_name = f'/{node_name}/change_state'
    cli = ros_node.create_client(ChangeState, service_name)
    if not cli.wait_for_service(timeout_sec=2.0):
        raise TimeoutError(f'lifecycle service {service_name} not available')
    req = ChangeState.Request()
    req.transition = Transition(id=transition_id)
    future = cli.call_async(req)
    loop = asyncio.get_event_loop()
    await loop.run_in_executor(
        None, lambda: rclpy.spin_until_future_complete(ros_node, future, timeout_sec=5.0))
    result = future.result()
    if result is None or not result.success:
        raise RuntimeError(f'lifecycle transition {transition_id} failed for {node_name}')
    return True


async def do_reload_node(node_name):
    """Chain: deactivate -> cleanup -> configure -> activate with 200ms delays."""
    DEACTIVATE = 4
    CLEANUP = 2
    CONFIGURE = 1
    ACTIVATE = 3
    for tid, label in [(DEACTIVATE, 'deactivate'), (CLEANUP, 'cleanup'),
                       (CONFIGURE, 'configure'), (ACTIVATE, 'activate')]:
        await call_lifecycle(node_name, tid)
        if ros_node:
            ros_node.get_logger().info(f'[reload] {node_name}: {label} -> ok')
        await asyncio.sleep(0.2)


# ── Background tasks ─────────────────────────────────────────────────────────

async def refresh_graph():
    """Periodically query the ROS2 graph for live nodes (every 3s)."""
    await asyncio.sleep(5.0)  # wait for DDS peer discovery on startup
    while True:
        await asyncio.sleep(3.0)
        if ros_node:
            try:
                names = await asyncio.get_event_loop().run_in_executor(
                    None, ros_node.get_node_names)
                live = set(names)
                with nodes_lock:
                    # Update in_graph flag for all known entries
                    for name in discovered_nodes:
                        discovered_nodes[name]['in_graph'] = name in live
                    # Add newly discovered nodes
                    for name in names:
                        if name not in discovered_nodes:
                            discovered_nodes[name] = {
                                'last_seen': 0.0,
                                'lifecycle_state': 'unknown',
                                'source': 'graph',
                                'in_graph': True,
                            }
                # Topic forwarder discovery (runs alongside graph query)
                ros_node._forwarder.discover()
            except Exception as e:
                if ros_node:
                    ros_node.get_logger().debug(f'[graph] query failed: {e}')


async def send_node_health():
    """Broadcast node health at 1 Hz.  Reports all discovered nodes — no hardcoded list."""
    while True:
        await asyncio.sleep(1.0)
        now = time.time()
        with nodes_lock:
            snap = dict(discovered_nodes)
        nodes_dict = {}
        for name, info in snap.items():
            in_g = info.get('in_graph', False)
            last_seen = info.get('last_seen', 0.0)
            age = now - last_seen if last_seen > 0 else 999.0
            if in_g or (last_seen > 0 and age < 2.0):
                status = 'OK'
            elif last_seen > 0 and age < 5.0:
                status = 'DEGRADED'
            elif last_seen > 0 and age < 10.0:
                status = 'LOST'
            else:
                status = 'OFFLINE'
            nodes_dict[name] = {
                'status': status,
                'last_seen_sec': round(age, 1) if last_seen > 0 else None,
                'lifecycle_state': info.get('lifecycle_state', 'unknown'),
                'in_graph': in_g,
                'last_error': info.get('last_error'),
            }
        await broadcast({'type': 'node_health', 'nodes': nodes_dict})


async def send_stub_data():
    """Send stub data for topics not yet implemented by real ROS2 nodes."""
    global stub_sim_time
    while True:
        await asyncio.sleep(2.0)
        if stub_sim_state == 'RUNNING':
            stub_sim_time += 2

        # Stubs only sent when real ROS2 data is absent
        snapshot = ros_node.get_snapshot() if ros_node else {}

        # Avionics stub — suppressed when /aircraft/controls/avionics is live
        if 'avionics' not in snapshot:
            await broadcast({
                'type': 'avionics',
                'com1_mhz': 118.10,
                'com2_mhz': 121.50,
                'com3_mhz': 0.0,
                'nav1_mhz': 109.10,
                'nav2_mhz': 110.30,
                'adf1_khz': 350,
                'adf2_khz': 0,
                'xpdr_code': 2000,
                'xpdr_mode': 'ALT',
                'obs1_deg': 0.0,
                'obs2_deg': 0.0,
                'dme_source': 0,
            })

        # Failure count stub — suppressed when real /sim/failures/state arrives
        if 'failure_state' not in snapshot:
            await broadcast({
                'type': 'failure_count',
                'armed': 0,
                'active': 0,
            })

        # Flight model stub — circle around EBBR for visual testing
        if 'flight_model_state' not in snapshot:
            t = stub_sim_time
            lat = 50.9014 + 0.05 * math.sin(t * 0.1)
            lon = 4.4844 + 0.05 * math.cos(t * 0.1)
            await broadcast({
                'type': 'flight_model_state',
                'lat': lat, 'lon': lon,
                'alt_ft_msl': 2500.0,
                'ias_kt': 90.0 if stub_sim_state == 'RUNNING' else 0.0,
                'gnd_speed_kt': 95.0 if stub_sim_state == 'RUNNING' else 0.0,
                'hdg_mag_deg': (t * 5.7) % 360,
                'track_deg': (t * 5.7) % 360,
                'vs_fpm': 0.0,
                'pitch_deg': 2.0 if stub_sim_state == 'RUNNING' else 0.0,
                'roll_deg': 0.0,
            })
        if 'sim_state' not in snapshot:
            await broadcast({
                'type': 'sim_state',
                'state': stub_sim_state,
                'aircraft_id': 'c172',
                'sim_time_sec': float(stub_sim_time),
                'time_scale': 1.0,
            })


# ── FastAPI ───────────────────────────────────────────────────────────────────

@app.get('/')
async def root():
    return JSONResponse({
        'service': 'ios_backend',
        'status': 'running',
        'websocket': 'ws://localhost:8080/ws',
    })


@app.get('/api/navaids/search')
async def search_navaids_endpoint(
    q: str = Query('', min_length=1),
    types: str = Query(''),
    limit: int = Query(20, ge=1, le=50),
):
    """Search navaid database via navaid_sim ROS2 service."""
    query = q.strip()
    if not query or len(query) < 2 or not ros_node:
        return JSONResponse([])

    try:
        cli = ros_node._search_navaids_cli
        if not cli.service_is_ready():
            return JSONResponse([])
        req = SearchNavaids.Request()
        req.query = query
        req.max_results = min(limit, 50)
        req.types = types
        future = cli.call_async(req)
        deadline = time.time() + 3.0
        while not future.done() and time.time() < deadline:
            await asyncio.sleep(0.05)
        if not future.done():
            return JSONResponse([])
        result = future.result()
        navaids = []
        for i in range(len(result.idents)):
            navaids.append({
                'ident': result.idents[i],
                'name': result.names[i],
                'type': result.types[i],
                'lat': result.latitudes[i],
                'lon': result.longitudes[i],
                'freq_mhz': result.frequencies_mhz[i],
                'range_nm': result.ranges_nm[i],
            })
        return JSONResponse(navaids)
    except Exception as e:
        if ros_node:
            ros_node.get_logger().warn(f'[search_navaids] error: {e}')
        return JSONResponse([])


def _airport_msg_to_dict(apt):
    """Convert a sim_msgs.msg.Airport to a JSON-serializable dict."""
    runways = []
    for r in apt.runways:
        runways.append({
            'designator_end1': r.designator_end1,
            'threshold_lat_deg_end1': r.threshold_lat_deg_end1,
            'threshold_lon_deg_end1': r.threshold_lon_deg_end1,
            'heading_deg_end1': r.heading_deg_end1,
            'displaced_threshold_m_end1': r.displaced_threshold_m_end1,
            'designator_end2': r.designator_end2,
            'threshold_lat_deg_end2': r.threshold_lat_deg_end2,
            'threshold_lon_deg_end2': r.threshold_lon_deg_end2,
            'heading_deg_end2': r.heading_deg_end2,
            'displaced_threshold_m_end2': r.displaced_threshold_m_end2,
            'width_m': r.width_m,
            'length_m': r.length_m,
            'surface_type': r.surface_type,
            'elevation_m': r.elevation_m,
        })
    return {
        'icao': apt.icao,
        'name': apt.name,
        'city': apt.city,
        'country': apt.country,
        'iata': apt.iata,
        'arp_lat_deg': apt.arp_lat_deg,
        'arp_lon_deg': apt.arp_lon_deg,
        'elevation_m': apt.elevation_m,
        'transition_altitude_ft': apt.transition_altitude_ft,
        'runways': runways,
    }


async def _handle_search_airports(ws, query, max_results):
    """Call SearchAirports service and send results to client."""
    if not ros_node or len(query) < 2:
        await ws.send_text(json.dumps({'type': 'airport_search_results', 'airports': []}))
        return
    try:
        cli = ros_node._search_airports_cli
        if not cli.service_is_ready():
            await ws.send_text(json.dumps({'type': 'airport_search_results', 'airports': []}))
            return
        req = SearchAirports.Request()
        req.query = query
        req.max_results = min(max_results, 8)
        future = cli.call_async(req)
        # Poll future — background rclpy.spin thread resolves it
        deadline = time.time() + 3.0
        while not future.done() and time.time() < deadline:
            await asyncio.sleep(0.05)
        if not future.done():
            await ws.send_text(json.dumps({'type': 'airport_search_results', 'airports': []}))
            return
        result = future.result()
        airports = [_airport_msg_to_dict(a) for a in (result.airports if result else [])]
        await ws.send_text(json.dumps({'type': 'airport_search_results', 'airports': airports}))
    except Exception as e:
        if ros_node:
            ros_node.get_logger().warn(f'[search_airports] error: {e}')
        await ws.send_text(json.dumps({'type': 'airport_search_results', 'airports': []}))


async def _resolve_ground_elevation_async(data: dict):
    """Returns (ground_elevation_m, lat_override_or_None, lon_override_or_None).

    Airport patches: look up ARP via SearchAirports; returns canonical ARP
    lat/lon so the caller can override user-supplied coords (airport DB is
    authoritative for airport patches).

    Custom patches: probe /navaid_sim/get_terrain_elevation at user-supplied
    lat/lon; returns (elevation, None, None).

    On service failure / not-found / timeout: returns (NaN, None, None) so
    the patch still creates — frontend can show the patch with AGL readout
    unavailable until the next re-resolve.
    """
    ptype = data.get('patch_type')

    if ptype == 'airport':
        icao = (data.get('icao') or '').strip().upper()
        if not ros_node or not icao:
            return (float('nan'), None, None)
        cli = ros_node._search_airports_cli
        if not cli.service_is_ready():
            ros_node.get_logger().warn(
                f'[patch] SearchAirports service not ready; '
                f'ground_elevation for {icao} set to NaN'
            )
            return (float('nan'), None, None)
        req = SearchAirports.Request()
        req.query = icao
        req.max_results = 1
        future = cli.call_async(req)
        deadline = time.monotonic() + 2.0
        while not future.done() and time.monotonic() < deadline:
            await asyncio.sleep(0.05)
        if not future.done():
            ros_node.get_logger().warn(f'[patch] SearchAirports timeout for {icao}')
            return (float('nan'), None, None)
        result = future.result()
        if not result or not result.airports:
            ros_node.get_logger().warn(
                f'[patch] ICAO {icao} not found; ground_elevation NaN'
            )
            return (float('nan'), None, None)
        ap = result.airports[0]
        return (float(ap.elevation_m), float(ap.arp_lat_deg), float(ap.arp_lon_deg))

    else:  # custom
        lat = data.get('lat_deg')
        lon = data.get('lon_deg')
        if lat is None or lon is None or not ros_node:
            return (float('nan'), None, None)
        cli = ros_node._terrain_elevation_cli
        if not cli.service_is_ready():
            ros_node.get_logger().warn(
                '[patch] GetTerrainElevation service not ready; '
                f'ground_elevation for ({float(lat):.4f},{float(lon):.4f}) NaN'
            )
            return (float('nan'), None, None)
        req = GetTerrainElevation.Request()
        req.latitude_deg = float(lat)
        req.longitude_deg = float(lon)
        future = cli.call_async(req)
        deadline = time.monotonic() + 2.0
        while not future.done() and time.monotonic() < deadline:
            await asyncio.sleep(0.05)
        if not future.done():
            ros_node.get_logger().warn(
                f'[patch] terrain probe timeout at ({float(lat):.4f},{float(lon):.4f})'
            )
            return (float('nan'), None, None)
        result = future.result()
        if not result or not result.valid:
            ros_node.get_logger().warn(
                f'[patch] terrain probe miss at ({float(lat):.4f},{float(lon):.4f}); NaN'
            )
            return (float('nan'), None, None)
        return (float(result.elevation_msl_m), None, None)


# ── Patch lifecycle async handlers (Slice 5c-refactor-I) ────────────────────
# reserve_patch + update_patch_identity are the SRTM/SearchAirports entry
# points. reserve allocates patch_id synchronously; ground resolution happens
# in the background via a follow-up update_patch_identity. Keeps the user-
# visible round-trip short (~10ms for patch_id) while ground elevation
# converges over ~100–500ms.

async def _resolve_and_followup_identity(patch_id: int, patch_data: dict):
    """Background task: resolve ground_elevation_m (and ARP lat/lon for
    airport patches) via SearchAirports / SRTM, then apply via
    update_patch_identity. Fire-and-forget — logs on error, doesn't retry.
    """
    if ros_node is None:
        return
    try:
        ground_m, lat_ov, lon_ov = await _resolve_ground_elevation_async(patch_data)
        update = {'patch_id': patch_id, 'ground_elevation_m': ground_m}
        if lat_ov is not None: update['lat_deg'] = lat_ov
        if lon_ov is not None: update['lon_deg'] = lon_ov
        ros_node.update_patch_identity(update)
    except Exception as e:
        if ros_node is not None:
            ros_node.get_logger().warn(
                f'[patch] background identity resolve failed for id={patch_id}: {e}')


async def _handle_reserve_patch(websocket, data: dict):
    """WS handler: validate + reserve synchronously, then spawn background
    ground-elevation resolution for patches that need it (custom patches
    without provided ground; airport patches without provided ground).
    Frontend is expected to pass ground_elevation_m from its airport DB
    hit for airport patches — in that case no background resolve fires.
    """
    if ros_node is None:
        return
    try:
        patch = ros_node.reserve_patch(dict(data))
    except ValueError as e:
        await websocket.send_text(json.dumps({
            'type': 'patch_error', 'operation': 'reserve', 'error': str(e),
        }))
        return

    # Background resolve only when caller didn't supply ground. Skip for
    # airport patches with valid ground already set.
    if 'ground_elevation_m' not in data:
        _spawn(_resolve_and_followup_identity(patch['patch_id'], dict(patch)))


async def _handle_update_patch_identity(websocket, data: dict):
    """WS handler: sync identity update, then background-resolve ground if
    lat/lon or icao changed and caller didn't supply ground.
    """
    if ros_node is None:
        return
    try:
        patch = ros_node.update_patch_identity(dict(data))
    except ValueError as e:
        await websocket.send_text(json.dumps({
            'type': 'patch_error', 'operation': 'update_identity', 'error': str(e),
        }))
        return

    coord_or_icao_changed = ('lat_deg' in data or 'lon_deg' in data
                             or 'icao' in data)
    if coord_or_icao_changed and 'ground_elevation_m' not in data:
        _spawn(_resolve_and_followup_identity(patch['patch_id'], dict(patch)))


async def _handle_get_runways(ws, icao):
    """Call GetRunways service and send results to client."""
    if not ros_node or not icao:
        await ws.send_text(json.dumps({'type': 'runway_results', 'found': False}))
        return
    try:
        cli = ros_node._get_runways_cli
        if not cli.service_is_ready():
            await ws.send_text(json.dumps({'type': 'runway_results', 'found': False}))
            return
        req = GetRunways.Request()
        req.icao = icao.upper()
        future = cli.call_async(req)
        deadline = time.time() + 3.0
        while not future.done() and time.time() < deadline:
            await asyncio.sleep(0.05)
        if not future.done():
            await ws.send_text(json.dumps({'type': 'runway_results', 'found': False}))
            return
        result = future.result()
        if result and result.found:
            await ws.send_text(json.dumps({
                'type': 'runway_results',
                'found': True,
                'airport': _airport_msg_to_dict(result.airport),
            }))
        else:
            await ws.send_text(json.dumps({'type': 'runway_results', 'found': False}))
    except Exception as e:
        if ros_node:
            ros_node.get_logger().warn(f'[get_runways] error: {e}')
        await ws.send_text(json.dumps({'type': 'runway_results', 'found': False}))


@app.on_event('startup')
async def startup_event():
    global ros_node
    rclpy.init()
    ros_node = IosBackendNode()

    # rclpy.spin() in a daemon thread does not work under uvicorn.
    # Use a dedicated thread with spin_once loop instead.
    def _spin_loop():
        import time as _time
        # Wait for DDS discovery before processing callbacks
        _time.sleep(3)
        while rclpy.ok():
            try:
                rclpy.spin_once(ros_node, timeout_sec=0.05)
            except Exception:
                pass

    threading.Thread(target=_spin_loop, daemon=True).start()
    asyncio.create_task(send_stub_data())
    asyncio.create_task(send_node_health())
    asyncio.create_task(refresh_graph())


@app.websocket('/ws')
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    connected_clients.add(websocket)

    async def sender():
        prev_json = {}
        last_send = {}   # key → monotonic time of last send
        # High-frequency topics throttled to 10 Hz; others sent immediately
        _THROTTLED = frozenset((
            'flight_model_state', 'electrical_state', 'nav_state',
            'air_data_state', 'engines_state', 'fuel_state', 'gear_state',
        ))
        _MIN_INTERVAL = 0.1  # 10 Hz
        while True:
            try:
                now = time.monotonic()
                snapshot = ros_node.get_snapshot() if ros_node else {}
                for key, data in snapshot.items():
                    if key in _THROTTLED:
                        if now - last_send.get(key, 0) < _MIN_INTERVAL:
                            continue
                    encoded = _dumps(data)
                    if prev_json.get(key) != encoded:
                        await websocket.send_text(encoded)
                        prev_json[key] = encoded
                        last_send[key] = now

                # Topic forwarder: topic_tree (every 3s) + topic_update (every cycle)
                if ros_node and hasattr(ros_node, '_forwarder'):
                    # topic_tree metadata — lightweight, every 3s
                    if now - last_send.get('_topic_tree', 0) >= 3.0:
                        tree = ros_node._forwarder.get_topic_tree()
                        if tree:
                            tree_msg = _dumps({'type': 'topic_tree', 'topics': tree})
                            if prev_json.get('_topic_tree') != tree_msg:
                                await websocket.send_text(tree_msg)
                                prev_json['_topic_tree'] = tree_msg
                            last_send['_topic_tree'] = now

                    # topic_update — raw SI values, every 200ms
                    if now - last_send.get('_topic_update', 0) >= 0.2:
                        values = ros_node._forwarder.get_all_values()
                        if values:
                            update_msg = _dumps({'type': 'topic_update', 'topics': values})
                            if prev_json.get('_topic_update') != update_msg:
                                await websocket.send_text(update_msg)
                                prev_json['_topic_update'] = update_msg
                            last_send['_topic_update'] = now

                await asyncio.sleep(0.05)
            except Exception as e:
                if ros_node:
                    ros_node.get_logger().warn(f'[ws-sender] exiting: {e}')
                break

    async def _safe_lifecycle(coro, cmd_id):
        """Run a lifecycle coroutine; on failure send command_error to this client."""
        try:
            await coro
        except Exception as e:
            if ros_node:
                ros_node.get_logger().warn(f'[cmd {cmd_id}] lifecycle error: {e}')
            try:
                await websocket.send_text(json.dumps({
                    'type': 'command_error', 'cmd': cmd_id, 'error': str(e),
                }))
            except Exception:
                pass  # client already disconnected

    send_task = asyncio.create_task(sender())
    try:
        while True:
            text = await websocket.receive_text()
            try:
                msg = json.loads(text)

                if msg.get('type') == 'command':
                    global stub_sim_state
                    cmd_id = msg.get('cmd')
                    payload = {k: v for k, v in msg.items() if k not in ('type', 'cmd')}

                    # Update stub state machine
                    if cmd_id == 1 and stub_sim_state in ('READY', 'FROZEN'):
                        stub_sim_state = 'RUNNING'
                    elif cmd_id == 2 and stub_sim_state == 'RUNNING':
                        stub_sim_state = 'FROZEN'
                    elif cmd_id == 3:
                        stub_sim_state = 'READY'

                    # Node lifecycle commands
                    node_name = payload.get('node_name', '')
                    if cmd_id == 7 and node_name:
                        asyncio.create_task(
                            _safe_lifecycle(do_reload_node(node_name), cmd_id))
                    elif cmd_id == 8 and node_name:
                        asyncio.create_task(
                            _safe_lifecycle(call_lifecycle(node_name, 4), cmd_id))
                    elif cmd_id == 9 and node_name:
                        asyncio.create_task(
                            _safe_lifecycle(call_lifecycle(node_name, 3), cmd_id))
                    elif cmd_id == 10 and node_name:
                        asyncio.create_task(
                            _safe_lifecycle(do_reload_node(node_name), cmd_id))

                    # Forward all commands to ROS2
                    try:
                        if ros_node:
                            ros_node.publish_command(cmd_id, payload if payload else None)
                    except Exception as e:
                        if ros_node:
                            ros_node.get_logger().warn(f'[cmd {cmd_id}] publish error: {e}')
                        await websocket.send_text(json.dumps({
                            'type': 'command_error', 'cmd': cmd_id, 'error': str(e),
                        }))

                elif msg.get('type') == 'failure_command' and ros_node:
                    ros_node.publish_failure_command(msg)

                elif msg.get('type') == 'set_avionics' and ros_node:
                    ros_node.publish_avionics(msg)

                elif msg.get('type') == 'set_virtual_avionics' and ros_node:
                    ros_node.publish_virtual_avionics(msg.get('data', msg))

                elif msg.get('type') == 'set_panel' and ros_node:
                    ros_node.publish_panel(msg.get('data', msg))

                elif msg.get('type') == 'set_virtual_panel' and ros_node:
                    ros_node.publish_virtual_panel(msg.get('data', msg))

                elif msg.get('type') == 'set_flight_controls' and ros_node:
                    ros_node.publish_flight_controls(msg.get('data', msg))

                elif msg.get('type') == 'set_virtual_flight_controls' and ros_node:
                    ros_node.publish_virtual_flight_controls(msg.get('data', msg))

                elif msg.get('type') == 'set_engine_controls' and ros_node:
                    ros_node.publish_engine_controls(msg.get('data', msg))

                elif msg.get('type') == 'set_virtual_engine_controls' and ros_node:
                    ros_node.publish_virtual_engine_controls(msg.get('data', msg))

                elif msg.get('topic') == '/aircraft/devices/virtual/panel' and ros_node:
                    ros_node.publish_virtual_panel(msg.get('data', {}))

                elif msg.get('type') == 'search_airports' and ros_node:
                    _spawn(_handle_search_airports(websocket, msg.get('query', ''),
                                                   msg.get('max_results', 8)))

                elif msg.get('type') == 'get_runways' and ros_node:
                    _spawn(_handle_get_runways(websocket, msg.get('icao', '')))

                elif msg.get('type') == 'freeze_position' and ros_node:
                    cmd_msg = SimCommand()
                    cmd_msg.header.stamp = ros_node.get_clock().now().to_msg()
                    cmd_msg.command = SimCommand.CMD_FREEZE_POSITION
                    ros_node._cmd_pub.publish(cmd_msg)

                elif msg.get('type') == 'freeze_fuel' and ros_node:
                    cmd_msg = SimCommand()
                    cmd_msg.header.stamp = ros_node.get_clock().now().to_msg()
                    cmd_msg.command = SimCommand.CMD_FREEZE_FUEL
                    ros_node._cmd_pub.publish(cmd_msg)

                elif msg.get('type') == 'set_departure' and ros_node:
                    # Reposition via CMD_REPOSITION - sim_manager owns the workflow
                    import math as _m
                    airspeed_ms = float(msg.get('airspeed_ms', 0))
                    # Configuration from frontend selector; fallback to inference from airspeed
                    config = msg.get('configuration', '')
                    if not config:
                        config = 'airborne_clean' if airspeed_ms > 1.0 else 'ready_for_takeoff'
                    payload = {
                        'latitude_deg': float(msg.get('lat_deg', 0)),
                        'longitude_deg': float(msg.get('lon_deg', 0)),
                        'altitude_msl_m': float(msg.get('alt_m', 0)),
                        'heading_rad': float(msg.get('heading_rad', 0)),
                        'airspeed_ms': airspeed_ms,
                        'configuration': config,
                    }
                    cmd_msg = SimCommand()
                    cmd_msg.header.stamp = ros_node.get_clock().now().to_msg()
                    cmd_msg.command = SimCommand.CMD_REPOSITION
                    cmd_msg.payload_json = json.dumps(payload)
                    ros_node._cmd_pub.publish(cmd_msg)
                    ros_node.get_logger().info(
                        f'[REPOSITION] lat={payload["latitude_deg"]:.5f} '
                        f'lon={payload["longitude_deg"]:.5f} alt={payload["altitude_msl_m"]:.1f}m '
                        f'hdg={payload["heading_rad"] * 180/_m.pi:.1f} '
                        f'config={config}')

                elif msg.get('type') == 'set_payload' and ros_node:
                    data = msg.get('data', {})
                    payload_msg = PayloadCommand()
                    payload_msg.header.stamp = ros_node.get_clock().now().to_msg()
                    for s in data.get('stations', []):
                        payload_msg.station_indices.append(int(s['index']))
                        payload_msg.weights_lbs.append(float(s['weight_lbs']))
                    ros_node._payload_pub.publish(payload_msg)

                elif msg.get('type') == 'set_fuel_loading' and ros_node:
                    data = msg.get('data', {})
                    fuel_msg = PayloadCommand()
                    fuel_msg.header.stamp = ros_node.get_clock().now().to_msg()
                    for t in data.get('tanks', []):
                        fuel_msg.station_indices.append(int(t['index']))
                        fuel_msg.weights_lbs.append(float(t.get('quantity_lbs', 0)))
                    ros_node._fuel_load_pub.publish(fuel_msg)

                elif msg.get('type') == 'set_weather' and ros_node:
                    ros_node.publish_weather(msg.get('data', msg))

                elif msg.get('type') == 'activate_microburst' and ros_node:
                    ros_node.activate_microburst(msg.get('data', {}))

                elif msg.get('type') == 'clear_microburst' and ros_node:
                    hid = msg.get('data', {}).get('hazard_id', 0)
                    ros_node.clear_microburst(int(hid))

                elif msg.get('type') == 'clear_all_microbursts' and ros_node:
                    ros_node.clear_all_microbursts()

                elif msg.get('type') == 'set_patch_microburst' and ros_node:
                    ros_node.set_patch_microburst(msg.get('data', {}))

                elif msg.get('type') == 'clear_patch_microburst' and ros_node:
                    ros_node.clear_patch_microburst(msg.get('data', {}))

                elif msg.get('type') == 'reserve_patch' and ros_node:
                    _spawn(_handle_reserve_patch(websocket, msg.get('data', {})))

                elif msg.get('type') == 'update_patch_identity' and ros_node:
                    _spawn(_handle_update_patch_identity(websocket, msg.get('data', {})))

                elif msg.get('type') == 'update_patch_overrides' and ros_node:
                    try:
                        ros_node.update_patch_overrides(msg.get('data', {}))
                    except ValueError as e:
                        await websocket.send_text(json.dumps({
                            'type': 'patch_error', 'operation': 'update_overrides',
                            'error': str(e),
                        }))

                elif msg.get('type') == 'remove_patch' and ros_node:
                    pid = int(msg.get('data', {}).get('patch_id', 0))
                    ok = ros_node.remove_patch(pid)
                    await websocket.send_text(json.dumps({
                        'type': 'patch_removed' if ok else 'patch_not_found',
                        'data': {'patch_id': pid},
                    }))

                elif msg.get('type') == 'clear_patches' and ros_node:
                    ros_node.clear_patches()
                    await websocket.send_text(json.dumps({
                        'type': 'patches_cleared',
                    }))

            except json.JSONDecodeError:
                pass
            except Exception as e:
                if ros_node:
                    ros_node.get_logger().warn(f'[ws] command handler error: {e}')
                try:
                    cmd_id = msg.get('cmd') if isinstance(msg, dict) else None
                    await websocket.send_text(json.dumps({
                        'type': 'command_error', 'cmd': cmd_id, 'error': str(e),
                    }))
                except Exception:
                    pass
    except WebSocketDisconnect:
        pass
    finally:
        send_task.cancel()
        connected_clients.discard(websocket)


# ── Entry point ──────────────────────────────────────────────────────────────
# When run via uvicorn (python3 -m uvicorn ios_backend.ios_backend_node:app),
# startup_event handles rclpy init. main() is only for ros2 run entrypoint.

def main(args=None):
    uvicorn.run(app, host='0.0.0.0', port=8080, log_level='info')


if __name__ == '__main__':
    main()
