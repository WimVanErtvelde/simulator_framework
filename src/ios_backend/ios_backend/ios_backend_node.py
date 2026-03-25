"""IOS Backend — FastAPI + rclpy WebSocket bridge.

Bridges ROS2 topics to/from browser clients via WebSocket.
- Subscribes to /sim/flight_model/state, /sim/fuel/state, /sim/state → forwards as JSON to WS clients
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

import yaml

import rclpy
from rclpy.node import Node
from rclpy.parameter import Parameter

from sim_msgs.msg import (FlightModelState, FuelState, SimState, SimCommand, PanelControls,
                          NavigationState, AvionicsControls, RawAvionicsControls,
                          RawFlightControls, RawEngineControls,
                          ElectricalState, SimAlert, EngineState,
                          FailureCommand, FailureState, TerrainSource,
                          InitialConditions, AirDataState)
from std_msgs.msg import String
from lifecycle_msgs.srv import ChangeState
from lifecycle_msgs.msg import Transition
from sim_msgs.srv import SearchAirports, GetRunways, SearchNavaids

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Query
from fastapi.responses import JSONResponse
import uvicorn





# ── ROS2 Node ────────────────────────────────────────────────────────────────

class IosBackendNode(Node):
    def __init__(self):
        super().__init__('ios_backend', parameter_overrides=[
            Parameter('use_sim_time', Parameter.Type.BOOL, True),
        ])
        self._latest = {}
        self._lock = threading.Lock()
        self._loaded_aircraft_id = ''

        self._flight_model_sub = self.create_subscription(
            FlightModelState, '/sim/flight_model/state', self._on_flight_model_state, 10)
        self._fuel_sub = self.create_subscription(
            FuelState, '/sim/fuel/state', self._on_fuel_state, 10)
        self._sim_state_sub = self.create_subscription(
            SimState, '/sim/state', self._on_sim_state, 10)
        self._nav_state_sub = self.create_subscription(
            NavigationState, '/sim/navigation/state', self._on_nav_state, 10)
        self._avionics_sub = self.create_subscription(
            AvionicsControls, '/sim/controls/avionics', self._on_avionics_controls, 10)
        self._elec_sub = self.create_subscription(
            ElectricalState, '/sim/electrical/state', self._on_electrical_state, 10)
        self._alert_sub = self.create_subscription(
            SimAlert, '/sim/alerts', self._on_sim_alert, 10)
        self._engine_sub = self.create_subscription(
            EngineState, '/sim/engines/state', self._on_engine_state, 10)
        self._heartbeat_sub = self.create_subscription(
            String, '/sim/diagnostics/heartbeat', self._on_heartbeat, 10)
        self._lifecycle_state_sub = self.create_subscription(
            String, '/sim/diagnostics/lifecycle_state', self._on_lifecycle_state, 10)

        # Failure state subscription
        self._failure_state_sub = self.create_subscription(
            FailureState, '/sim/failure_state', self._on_failure_state, 10)

        # Terrain source subscription
        self._terrain_source_sub = self.create_subscription(
            TerrainSource, '/sim/terrain/source', self._on_terrain_source, 10)

        # Air data subscription (pitot-static instruments)
        self._air_data_sub = self.create_subscription(
            AirDataState, '/sim/air_data/state', self._on_air_data_state, 10)

        # Failure command publisher → sim_failures
        self._failure_cmd_pub = self.create_publisher(
            FailureCommand, '/ios/failure_command', 10)

        # IOS instructor panel — highest priority in arbitrator
        self._panel_pub = self.create_publisher(
            PanelControls, '/devices/instructor/panel', 10)
        # Virtual cockpit panel — lower priority, used by /cockpit/* pages
        self._virtual_panel_pub = self.create_publisher(
            PanelControls, '/devices/virtual/panel', 10)
        self._raw_avionics_pub = self.create_publisher(
            RawAvionicsControls, '/devices/instructor/controls/avionics', 10)
        self._cmd_pub = self.create_publisher(
            SimCommand, '/sim/command', 10)
        # Instructor flight/engine controls — arbitrated by input_arbitrator
        self._raw_flight_pub = self.create_publisher(
            RawFlightControls, '/devices/instructor/controls/flight', 10)
        self._raw_engine_pub = self.create_publisher(
            RawEngineControls, '/devices/instructor/controls/engine', 10)
        self._heartbeat_pub = self.create_publisher(
            String, '/sim/diagnostics/heartbeat', 10)
        self._lifecycle_pub = self.create_publisher(
            String, '/sim/diagnostics/lifecycle_state', 10)

        # Service clients for airport/runway queries
        self._search_airports_cli = self.create_client(
            SearchAirports, '/navaid_sim/search_airports')
        self._get_runways_cli = self.create_client(
            GetRunways, '/navaid_sim/get_runways')
        self._search_navaids_cli = self.create_client(
            SearchNavaids, '/navaid_sim/search_navaids')

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

        self.get_logger().info('ios_backend started — ws://0.0.0.0:8080/ws')

    def _publish_heartbeat(self):
        msg = String()
        msg.data = self.get_name()
        self._heartbeat_pub.publish(msg)

    def _publish_lifecycle(self, state: str):
        msg = String()
        msg.data = f'{self.get_name()}:{state}'
        self._lifecycle_pub.publish(msg)

    def _on_flight_model_state(self, msg: FlightModelState):
        import math as _m
        data = {
            'type': 'flight_model_state',
            'lat': float(msg.latitude_deg),
            'lon': float(msg.longitude_deg),
            'alt_ft_msl': float(msg.altitude_msl_m) * 3.28084,
            'ias_kt': float(msg.ias_ms) * 1.94384,
            'gnd_speed_kt': float(msg.ground_speed_ms) * 1.94384,
            'hdg_mag_deg': float(msg.magnetic_heading_rad) * 180.0 / _m.pi,
            'track_deg': float(msg.ground_track_rad) * 180.0 / _m.pi,
            'vs_fpm': float(msg.vertical_speed_ms) * 196.85,  # m/s to ft/min
            'pitch_deg': float(msg.pitch_rad) * 180.0 / _m.pi,
            'roll_deg': float(msg.roll_rad) * 180.0 / _m.pi,
            'is_helicopter': bool(msg.is_helicopter),
        }
        with self._lock:
            self._latest['flight_model_state'] = data

    def _on_fuel_state(self, msg: FuelState):
        n = int(msg.tank_count) if msg.tank_count > 0 else 4
        data = {
            'type': 'fuel_state',
            'tank_count': n,
            'density_kg_per_liter': float(msg.density_kg_per_liter),
            'fuel_type': msg.fuel_type,
            'tank_quantity_kg': [float(msg.tank_quantity_kg[i]) for i in range(n)],
            'tank_quantity_pct': [float(msg.tank_quantity_pct[i]) for i in range(n)],
            'tank_usable_kg': [float(msg.tank_usable_kg[i]) for i in range(n)],
            'tank_quantity_liters': [float(msg.tank_quantity_liters[i]) for i in range(n)],
            'total_fuel_kg': float(msg.total_fuel_kg),
            'total_fuel_pct': float(msg.total_fuel_pct),
            'total_fuel_liters': float(msg.total_fuel_liters),
            'engine_fuel_flow_kgs': [float(x) for x in msg.engine_fuel_flow_kgs],
            'engine_fuel_flow_lph': [float(x) for x in msg.engine_fuel_flow_lph],
            'fuel_pressure_pa': [float(msg.fuel_pressure_pa[i]) for i in range(n)],
            'boost_pump_on': [bool(msg.boost_pump_on[i]) for i in range(n)],
            'tank_selected': [bool(msg.tank_selected[i]) for i in range(n)],
            'low_fuel_warning': bool(msg.low_fuel_warning),
            'cg_contribution_m': float(msg.cg_contribution_m),
        }
        with self._lock:
            self._latest['fuel_state'] = data

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
            'sim_time_sec': float(msg.sim_time_sec),
            'time_scale': float(msg.time_scale),
        }
        with self._lock:
            self._latest['sim_state'] = data
            # Load avionics config when aircraft_id changes
            if msg.aircraft_id and msg.aircraft_id != self._loaded_aircraft_id:
                self._load_avionics_config(msg.aircraft_id)
                self._load_engine_config(msg.aircraft_id)
                self._load_fuel_config(msg.aircraft_id)
                self._load_failures_config(msg.aircraft_id)

    def _on_nav_state(self, msg: NavigationState):
        xpdr_modes = {0: 'OFF', 1: 'STBY', 2: 'ON', 3: 'ALT'}
        to_from_names = {0: 'OFF', 1: 'TO', 2: 'FROM'}
        nav_type_names = {0: 'NONE', 1: 'VOR', 2: 'VORDME', 3: 'ILS', 4: 'LOC'}
        dme_src_names = {0: 'NAV1', 1: 'HOLD', 2: 'NAV2'}
        data = {
            'type': 'nav_state',
            # GPS1
            'gps1_valid': bool(msg.gps1_valid),
            'gps1_lat_deg': float(msg.gps1_lat_deg),
            'gps1_lon_deg': float(msg.gps1_lon_deg),
            'gps1_alt_ft': float(msg.gps1_alt_ft),
            'gps1_gs_kt': float(msg.gps1_gs_kt),
            'gps1_track_deg': float(msg.gps1_track_deg),
            # GPS2
            'gps2_valid': bool(msg.gps2_valid),
            'gps2_lat_deg': float(msg.gps2_lat_deg),
            'gps2_lon_deg': float(msg.gps2_lon_deg),
            'gps2_alt_ft': float(msg.gps2_alt_ft),
            'gps2_gs_kt': float(msg.gps2_gs_kt),
            'gps2_track_deg': float(msg.gps2_track_deg),
            'active_gps_source': int(msg.active_gps_source),
            # NAV1
            'nav1_valid': bool(msg.nav1_valid),
            'nav1_ident': msg.nav1_ident,
            'nav1_type': nav_type_names.get(msg.nav1_type, 'NONE'),
            'nav1_obs_deg': float(msg.nav1_obs_deg),
            'nav1_cdi_dots': float(msg.nav1_cdi_dots),
            'nav1_bearing_deg': float(msg.nav1_bearing_deg),
            'nav1_radial_deg': float(msg.nav1_radial_deg),
            'nav1_distance_nm': float(msg.nav1_distance_nm),
            'nav1_signal': float(msg.nav1_signal_strength),
            'nav1_to_from': to_from_names.get(msg.nav1_to_from, 'OFF'),
            'nav1_gs_valid': bool(msg.nav1_gs_valid),
            'nav1_gs_dots': float(msg.nav1_gs_dots),
            # NAV2
            'nav2_valid': bool(msg.nav2_valid),
            'nav2_ident': msg.nav2_ident,
            'nav2_type': nav_type_names.get(msg.nav2_type, 'NONE'),
            'nav2_obs_deg': float(msg.nav2_obs_deg),
            'nav2_cdi_dots': float(msg.nav2_cdi_dots),
            'nav2_bearing_deg': float(msg.nav2_bearing_deg),
            'nav2_radial_deg': float(msg.nav2_radial_deg),
            'nav2_distance_nm': float(msg.nav2_distance_nm),
            'nav2_signal': float(msg.nav2_signal_strength),
            'nav2_to_from': to_from_names.get(msg.nav2_to_from, 'OFF'),
            'nav2_gs_valid': bool(msg.nav2_gs_valid),
            'nav2_gs_dots': float(msg.nav2_gs_dots),
            # ADF1
            'adf1_valid': bool(msg.adf1_valid),
            'adf1_ident': msg.adf1_ident,
            'adf1_rel_bearing_deg': float(msg.adf1_relative_bearing_deg),
            'adf1_signal': float(msg.adf1_signal),
            # ADF2
            'adf2_valid': bool(msg.adf2_valid),
            'adf2_ident': msg.adf2_ident,
            'adf2_rel_bearing_deg': float(msg.adf2_relative_bearing_deg),
            'adf2_signal': float(msg.adf2_signal),
            # DME
            'dme_source': dme_src_names.get(msg.dme_source, 'NAV1'),
            'dme_valid': bool(msg.dme_valid),
            'dme_distance_nm': float(msg.dme_distance_nm),
            'dme_gs_kt': float(msg.dme_gs_kt),
            # TACAN
            'tacan_valid': bool(msg.tacan_valid),
            'tacan_ident': msg.tacan_ident,
            'tacan_bearing_deg': float(msg.tacan_bearing_deg),
            'tacan_distance_nm': float(msg.tacan_distance_nm),
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
        }
        with self._lock:
            self._latest['nav_state'] = data

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
            'obs1': float(msg.obs1),
            'obs2': float(msg.obs2),
            'dme_source': int(msg.dme_source),
            'tacan_channel': int(msg.tacan_channel),
            'tacan_band': int(msg.tacan_band),
            'gps_source': int(msg.gps_source),
        }
        with self._lock:
            self._latest['avionics'] = data

    def _on_electrical_state(self, msg: ElectricalState):
        data = {
            'type': 'electrical_state',
            'bus_names': list(msg.bus_names),
            'bus_voltages': [float(v) for v in msg.bus_voltages],
            'bus_powered': [bool(p) for p in msg.bus_powered],
            'source_names': list(msg.source_names),
            'source_active': [bool(a) for a in msg.source_active],
            'source_voltages': [float(v) for v in msg.source_voltages],
            'source_currents': [float(c) for c in msg.source_currents],
            'load_names': list(msg.load_names),
            'load_powered': [bool(p) for p in msg.load_powered],
            'load_currents': [float(c) for c in msg.load_currents],
            'switch_ids': list(msg.switch_ids),
            'switch_labels': list(msg.switch_labels),
            'switch_closed': [bool(c) for c in msg.switch_closed],
            'total_load_amps': float(msg.total_load_amps),
            'battery_soc_pct': float(msg.battery_soc_pct),
            'master_bus_voltage': float(msg.master_bus_voltage),
            'avionics_bus_powered': bool(msg.avionics_bus_powered),
            'essential_bus_powered': bool(msg.essential_bus_powered),
        }
        with self._lock:
            self._latest['electrical_state'] = data

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

    def _on_engine_state(self, msg: EngineState):
        n = msg.engine_count
        # Map engine_state enum to running/failed bools for frontend compatibility
        running = [msg.engine_state[i] == 3 for i in range(n)]  # STATE_RUNNING=3
        failed = [msg.engine_state[i] == 5 for i in range(n)]   # STATE_FAILED=5
        data = {
            'type': 'engines_state',
            'engine_count': int(n),
            'engine_type': msg.engine_type,
            'rpm': [float(msg.engine_rpm[i]) for i in range(n)],
            'egt_degc': [float(msg.egt_degc[i]) for i in range(n)],
            'cht_degc': [float(msg.cht_degc[i]) for i in range(n)],
            'oil_pressure_psi': [float(msg.oil_press_kpa[i] * 0.145038) for i in range(n)],
            'oil_temp_degc': [float(msg.oil_temp_degc[i]) for i in range(n)],
            'manifold_pressure_inhg': [float(msg.manifold_press_inhg[i]) for i in range(n)],
            'fuel_flow_gph': [float(msg.fuel_flow_kgph[i] / 2.7216) for i in range(n)],
            'n1_pct': [float(msg.n1_pct[i]) for i in range(n)],
            'n2_pct': [float(msg.n2_pct[i]) for i in range(n)],
            'tgt_degc': [float(msg.tot_degc[i]) for i in range(n)],
            'torque_pct': [float(msg.torque_pct[i]) for i in range(n)],
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
        """Load fuel config from aircraft package fuel.yaml."""
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
            tanks = fuel.get('tanks', [])
            data = {
                'type': 'fuel_config',
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
            self._latest['fuel_config'] = data
            self.get_logger().info(
                f'Loaded fuel config for {aircraft_id}: '
                f'{data["fuel_type"]}, {data["tank_count"]} tank(s), density={density}')
        except Exception as e:
            self.get_logger().error(f'Failed to load fuel config: {e}')

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

    def _on_air_data_state(self, msg: AirDataState):
        data = {
            'type': 'air_data_state',
            'ias_kt': msg.indicated_airspeed_ms[0] * 1.94384,
            'cas_kt': msg.calibrated_airspeed_ms[0] * 1.94384,
            'mach': msg.mach[0],
            'alt_indicated_ft': msg.altitude_indicated_m[0] * 3.28084,
            'alt_pressure_ft': msg.altitude_pressure_m[0] * 3.28084,
            'vs_fpm': msg.vertical_speed_ms[0] * 196.85,
            'sat_c': msg.sat_k[0] - 273.15,
            'tat_c': msg.tat_k[0] - 273.15,
            'pitot_healthy': bool(msg.pitot_healthy[0]),
            'static_healthy': bool(msg.static_healthy[0]),
            'pitot_heat_on': bool(msg.pitot_heat_on[0]),
            'pitot_ice_pct': float(msg.pitot_ice_pct[0]),
        }
        with self._lock:
            self._latest['air_data_state'] = data

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

    def publish_failure_command(self, data: dict):
        """Publish a FailureCommand to /ios/failure_command."""
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
        """Publish RawAvionicsControls to /devices/instructor/controls/avionics."""
        msg = RawAvionicsControls()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.com1_freq_mhz = float(data.get('com1_mhz', 118.10))
        msg.com2_freq_mhz = float(data.get('com2_mhz', 121.50))
        msg.com3_freq_mhz = float(data.get('com3_mhz', 0.0))
        msg.nav1_freq_mhz = float(data.get('nav1_mhz', 109.10))
        msg.nav2_freq_mhz = float(data.get('nav2_mhz', 110.30))
        msg.obs1 = float(data.get('obs1', 0.0))
        msg.obs2 = float(data.get('obs2', 0.0))
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

    def publish_flight_controls(self, data: dict):
        """Publish RawFlightControls to /devices/instructor/controls/flight."""
        msg = RawFlightControls()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.aileron       = float(data.get('aileron', 0.0))
        msg.elevator      = float(data.get('elevator', 0.0))
        msg.rudder        = float(data.get('rudder', 0.0))
        msg.collective    = float(data.get('collective', 0.0))
        msg.trim_aileron  = float(data.get('trim_aileron', 0.0))
        msg.trim_elevator = float(data.get('trim_elevator', 0.0))
        msg.trim_rudder   = float(data.get('trim_rudder', 0.0))
        msg.brake_left    = float(data.get('brake_left', 0.0))
        msg.brake_right   = float(data.get('brake_right', 0.0))
        self._raw_flight_pub.publish(msg)

    def publish_engine_controls(self, data: dict):
        """Publish RawEngineControls to /devices/instructor/controls/engine."""
        msg = RawEngineControls()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.throttle = [float(t) for t in data.get('throttle', [0.0])]
        msg.mixture  = [float(m) for m in data.get('mixture', [1.0])]
        msg.condition = [float(c) for c in data.get('condition', [])]
        msg.prop_rpm  = [float(r) for r in data.get('prop_rpm', [])]
        msg.magneto_left  = [bool(m) for m in data.get('magneto_left', [])]
        msg.magneto_right = [bool(m) for m in data.get('magneto_right', [])]
        msg.starter = bool(data.get('starter', False))
        self._raw_engine_pub.publish(msg)

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
        msg.selector_ids = data.get('selector_ids', [])
        msg.selector_values = data.get('selector_values', [])
        msg.pot_ids = data.get('pot_ids', [])
        msg.pot_values = data.get('pot_values', [])
        msg.encoder_abs_ids = data.get('encoder_abs_ids', [])
        msg.encoder_abs_values = data.get('encoder_abs_values', [])
        msg.encoder_rel_ids = data.get('encoder_rel_ids', [])
        msg.encoder_rel_deltas = data.get('encoder_rel_deltas', [])
        self._panel_pub.publish(msg)

    def publish_virtual_panel(self, data: dict):
        """Publish PanelControls to /devices/virtual/panel (cockpit pages)."""
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

        # Avionics stub — suppressed when /sim/controls/avionics is live
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
                'obs1': 0.0,
                'obs2': 0.0,
                'dme_source': 0,
            })

        # Failure count stub — suppressed when real /sim/failure_state arrives
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
        send_count = 0
        while True:
            try:
                snapshot = ros_node.get_snapshot() if ros_node else {}
                for key, data in snapshot.items():
                    encoded = _dumps(data)
                    if prev_json.get(key) != encoded:
                        await websocket.send_text(encoded)
                        prev_json[key] = encoded
                        send_count += 1
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

                elif msg.get('type') == 'set_panel' and ros_node:
                    ros_node.publish_panel(msg.get('data', msg))

                elif msg.get('type') == 'set_virtual_panel' and ros_node:
                    ros_node.publish_virtual_panel(msg.get('data', msg))

                elif msg.get('type') == 'set_flight_controls' and ros_node:
                    ros_node.publish_flight_controls(msg.get('data', msg))

                elif msg.get('type') == 'set_engine_controls' and ros_node:
                    ros_node.publish_engine_controls(msg.get('data', msg))

                elif msg.get('topic') == '/devices/virtual/panel' and ros_node:
                    ros_node.publish_virtual_panel(msg.get('data', {}))

                elif msg.get('type') == 'search_airports' and ros_node:
                    asyncio.create_task(
                        _handle_search_airports(websocket, msg.get('query', ''),
                                                msg.get('max_results', 8)))

                elif msg.get('type') == 'get_runways' and ros_node:
                    asyncio.create_task(
                        _handle_get_runways(websocket, msg.get('icao', '')))

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
