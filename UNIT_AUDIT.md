# Unit Conversion Audit

Generated: 2026-03-24
Updated: 2026-03-27 (V3 unit suffix cleanup complete)
Scope: All C++/Python/JS source files under src/ and ios/frontend/

---

## 1. Wire format (FlightModelState.msg)

All SI units unless noted. Field name suffix indicates unit.

| Field | Unit | Suffix |
|---|---|---|
| latitude_deg / longitude_deg | degrees | _deg |
| altitude_msl_m / altitude_agl_m | metres | _m |
| ecef_x/y/z_m | metres | _m |
| roll_rad / pitch_rad / true_heading_rad | radians | _rad |
| vel_north/east/down_ms, vel_u/v/w_ms | m/s | _ms |
| accel_x/y/z_ms2 | m/s^2 | _ms2 |
| ias_ms / tas_ms / cas_ms / eas_ms / ground_speed_ms | m/s | _ms |
| vertical_speed_ms / wind_speed_ms | m/s | _ms |
| dynamic_pressure_pa / static_pressure_pa | Pa | _pa |
| air_density_kgm3 | kg/m^3 | _kgm3 |
| temperature_k | K | _k |
| fuel_total_kg / fuel_tank_kg[] / total_mass_kg | kg | _kg |
| fuel_total_norm | 0-1 ratio | _norm |
| fuel_flow_kgs[] | kg/s | _kgs |
| oil_pressure_pa[] | Pa | _pa |
| oil_temperature_k[] / iat_k[] / itt_k[] | K | _k |
| engine_egt_degc[] / engine_cht_degc[] / engine_oil_temp_degc[] | degC | _degc |
| engine_oil_pressure_psi[] | PSI | _psi |
| engine_manifold_pressure_inhg[] | inHg | _inhg |
| torque_nm[] | N*m | _nm |
| wheel_angle_deg[] | degrees | _deg |
| gear_position_norm[] | 0-1 ratio | _norm |
| throttle_pct[] | 0-100% | _pct |
| flap_pct | 0-100% | _pct |
| speed_brake_pct | 0-100% | _pct |

**Mixed units in same message (by design):**
SI fields (_pa, _k, _ms) coexist with instrument fields (_psi, _degc, _inhg) for engine gauges. The SI fields are backfills for interop; instrument fields are what cockpit displays read directly.

---

## 2. Unit suffix conventions (LOCKED)

| Suffix | Meaning | Range |
|--------|---------|-------|
| `_norm` | Normalized unitless | 0.0-1.0 or -1.0 to +1.0 |
| `_pct` | Percentage | 0.0-100.0 |
| `_v` | Volts | any |
| `_a` | Amps | any |
| `_deg` | Degrees (angle) | any |
| `_rpm` | RPM | >= 0 |
| `_m` | Metres | any |
| No suffix | Dimensionless, bool, enum, string, count | -- |

**Rule:** If a field stores 0-1, it MUST use `_norm`. If it stores 0-100, it uses `_pct`.

---

## 3. Conversion constants found

### JSBSimAdapter.cpp (lines 19-38)

| Constant | Value | Direction | Context |
|---|---|---|---|
| FT_TO_M | 0.3048 | ft->m | All length: altitude, velocity, acceleration |
| LBS_TO_KG | 0.453592 | lbs->kg | Fuel tank, total mass |
| KG_TO_LBS | 1/0.453592 | kg->lbs | Fuel writeback to JSBSim |
| LBF_TO_N | 4.44822 | lbf->N | Force |
| KTS_TO_MS | 0.514444 | kts->m/s | Airspeeds |
| PSF_TO_PA | 47.880258 | psf->Pa | Dynamic/static pressure |
| SLUG_TO_KG | 14.5939 | slug->kg | Mass |
| SLUGFT3_TO_KGM3 | 515.379 | slug/ft^3->kg/m^3 | Air density |
| PSI_TO_PA | 6894.76 | psi->Pa | Oil pressure (SI backfill) |
| R_TO_K | 5/9 | Rankine->K factor | Temperature |
| DEGF_TO_DEGC_SCALE | 5/9 | degF->degC factor | Engine temps |
| DEGF_TO_DEGC_OFFSET | 32 | degF->degC offset | Engine temps |
| DEG_TO_RAD | pi/180 | deg->rad | IC angles, heading |
| AVGAS_DENSITY_KG_L | 0.72 | kg/litre | Fuel drain calculation |

### failures_node.cpp (lines 79-81, named constants)

| Constant | Value | Direction | Context |
|---|---|---|---|
| MS_TO_KT | 1.94384 | m/s->kt | Condition trigger: airspeed |
| M_TO_FT | 3.28084 | m->ft | Condition trigger: altitude |
| MS_TO_FPM | 196.85 | m/s->fpm | Condition trigger: VS |

### navigation_node.cpp, cigi_host_node.cpp, engines_model.cpp

All use named constants. See source files for full listing.

---

## 4. Resolved issues

All issues from the original audit have been addressed:

| Issue | Status | Resolution |
|---|---|---|
| `_pct` suffix on 0-1 fields | **RESOLVED** | Renamed to `_norm` (V3, 2026-03-27) |
| HatHotResponse fields lack suffix | **RESOLVED** | Renamed to lat_deg, lon_deg, hat_m, hot_m (2026-03-24) |
| Hardcoded literals in failures_node.cpp | **RESOLVED** | Named constants MS_TO_KT, M_TO_FT, MS_TO_FPM |
| AvionicsControls obs1/obs2 missing suffix | **RESOLVED** | Renamed to obs1_deg, obs2_deg (V3 batch 4) |
| ElectricalState fields lack unit suffix | **RESOLVED** | bus_voltages_v, source_currents_a, etc. (V3 batch 3) |
| FlightControls/EngineControls no suffix | **RESOLVED** | All fields renamed with _norm suffix (V3 batch 1+2) |
| Lat/lon radians on wire | **RESOLVED** | All ROS2 lat/lon in degrees (2026-03-24 refactor) |

### Deferred (correct but wasteful)

**Oil pressure triple conversion:**
JSBSim(psi) -> FMS.oil_pressure_pa(Pa) -> EngineState.oil_press_kpa(kPa) -> WS(psi).
The value starts and ends in PSI but gets converted 3 times. The instrument field
(engine_oil_pressure_psi) avoids this. Fix when next touching the oil pressure chain.

---

## 5. Message field inventory (updated 2026-03-27)

### FlightModelState.msg
SI primary (m, m/s, rad, Pa, K, kg, kg/s) with instrument backfills (_psi, _degc, _inhg).
`_norm` for ratios (fuel_total_norm, gear_position_norm). `_pct` for 0-100 (throttle_pct, flap_pct).

### FuelState.msg
kg, litres, lph, kgs, Pa, `_norm` for ratios (tank_quantity_norm, total_fuel_norm).

### EngineState.msg
kPa, degC, inhg, kgph, kw, rpm, `_pct` (n1_pct, n2_pct, torque_pct — all 0-100), nm, deg.

### ElectricalState.msg
`_v` for voltages (bus_voltages_v, source_voltages_v, master_bus_voltage_v).
`_a` for currents (source_currents_a, load_currents_a, total_load_a).
`_pct` for battery_soc_pct (0-100).

### AirDataState.msg
ms, m, K, `_norm` for pitot_ice_norm (0-1).

### GearState.msg
`_norm` for position (position_norm, brake_left_norm, brake_right_norm). _deg for nosewheel.

### NavigationState.msg
deg, nm, kt, mhz, khz, dots. Dimensionless: signal_strength, transponder_code.

### AvionicsControls.msg
mhz, khz, `_deg` for OBS (obs1_deg, obs2_deg). Dimensionless: transponder_code, tacan_channel.

### FlightControls.msg + RawFlightControls.msg
All axes: `_norm` suffix. Booleans: gear_down, parking_brake, rotor_brake (no suffix).

### EngineControls.msg + RawEngineControls.msg
All levers: `_norm` suffix (throttle_norm, mixture_norm, condition_norm, prop_lever_norm).

### InitialConditions.msg
_deg, _m, _rad, _ms, _pa, _k. `_norm` for fuel_total_norm (0-1).

### AtmosphereState.msg
K, Pa, kgm3, ms, m. All properly suffixed.
