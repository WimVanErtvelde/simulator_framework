# Unit Conversion Audit

Generated: 2026-03-24
Scope: All C++/Python/JS source files under src/ and ios/frontend/

---

## 1. Wire format (FlightModelState.msg)

All SI units unless noted. Field name suffix indicates unit.

| Field | Unit | Suffix |
|---|---|---|
| latitude_rad / longitude_rad | radians | _rad |
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
| fuel_flow_kgs[] | kg/s | _kgs |
| oil_pressure_pa[] | Pa | _pa |
| oil_temperature_k[] / iat_k[] / itt_k[] | K | _k |
| engine_egt_degc[] / engine_cht_degc[] / engine_oil_temp_degc[] | degC | _degc |
| engine_oil_pressure_psi[] | PSI | _psi |
| engine_manifold_pressure_inhg[] | inHg | _inhg |
| torque_nm[] | N*m | _nm |
| wheel_angle_deg[] | degrees | _deg |

**Mixed units in same message (by design):**
SI fields (_pa, _k, _ms) coexist with instrument fields (_psi, _degc, _inhg) for engine gauges. The SI fields are backfills for interop; instrument fields are what cockpit displays read directly.

---

## 2. Conversion constants found

### JSBSimAdapter.cpp (lines 19-38)

| Constant | Value | Direction | Context |
|---|---|---|---|
| FT_TO_M | 0.3048 | ft→m | All length: altitude, velocity, acceleration |
| LBS_TO_KG | 0.453592 | lbs→kg | Fuel tank, total mass |
| KG_TO_LBS | 1/0.453592 | kg→lbs | Fuel writeback to JSBSim |
| LBF_TO_N | 4.44822 | lbf→N | Force |
| KTS_TO_MS | 0.514444 | kts→m/s | Airspeeds |
| PSF_TO_PA | 47.880258 | psf→Pa | Dynamic/static pressure |
| SLUG_TO_KG | 14.5939 | slug→kg | Mass |
| SLUGFT3_TO_KGM3 | 515.379 | slug/ft^3→kg/m^3 | Air density |
| PSI_TO_PA | 6894.76 | psi→Pa | Oil pressure (SI backfill) |
| INHG_TO_PA | 3386.39 | inHg→Pa | Unused (atmospheric) |
| R_TO_K | 5/9 | Rankine→K factor | Temperature |
| DEGF_TO_DEGC_SCALE | 5/9 | degF→degC factor | Engine temps (EGT, CHT, oil) |
| DEGF_TO_DEGC_OFFSET | 32 | degF→degC offset | Engine temps |
| K_TO_DEGC_OFFSET | 273.15 | K→degC offset | SI backfill |
| DEG_TO_RAD | pi/180 | deg→rad | IC angles, heading |
| AVGAS_DENSITY_KG_L | 0.72 | kg/litre | Fuel drain calculation |

### navigation_node.cpp (lines 17-20)

| Constant | Value | Direction | Context |
|---|---|---|---|
| RAD_TO_DEG | 180/pi | rad→deg | GPS output, VOR radials |
| M_TO_NM | 1/1852 | m→NM | DME distance |
| M_TO_FT | 3.28084 | m→ft | GPS altitude |
| MS_TO_KT | 1.94384 | m/s→kt | GPS ground speed |

### cigi_host_node.cpp (lines 46-50)

| Constant | Value | Direction | Context |
|---|---|---|---|
| RAD_TO_DEG | 180/pi | rad→deg | Entity Control lat/lon/attitude |
| DEG_TO_RAD | pi/180 | deg→rad | HOT request tracker |
| EARTH_A | 6378137.0 | metres | Body-to-latlon offset |

### AirportDatabase.cpp (lines 10-12)

| Constant | Value | Direction | Context |
|---|---|---|---|
| FT2M | 0.3048 | ft→m | A424 elevation, runway length |
| DEG2RAD | pi/180 | deg→rad | Lat/lon conversion |

### atmosphere_node.cpp (lines 15-31)

| Constant | Value | Context |
|---|---|---|
| ISA_T0 | 288.15 K | Sea-level temp |
| ISA_P0 | 101325.0 Pa | Sea-level pressure |
| ISA_RHO0 | 1.225 kg/m^3 | Sea-level density |
| ISA_LAPSE | 0.0065 K/m | Troposphere lapse |
| ISA_G | 9.80665 m/s^2 | Standard gravity |

### engines_model.cpp (C172 plugin, lines 12-13)

| Constant | Value | Direction | Context |
|---|---|---|---|
| PSI_TO_KPA | 6.89476 | psi→kPa | Oil pressure for EngineState |
| GPH_TO_KGPH | 2.72 | gal/h→kg/h | Fuel flow (avgas 0.72 kg/L) |

### ios_backend_node.py (inline, lines 261-270)

| Literal | Value | Direction | Context |
|---|---|---|---|
| 180.0/pi | ~57.296 | rad→deg | Lat, lon, heading, pitch, roll |
| 3.28084 | m→ft | Altitude MSL |
| 1.94384 | m/s→kt | IAS, ground speed |
| 196.85 | m/s→fpm | Vertical speed |
| 0.145038 | kPa→psi | Oil pressure display |
| 2.7216 | kg/h→gal/h | Fuel flow display |
| 273.15 | K→degC offset | SAT, TAT |

### failures_node.cpp (inline, lines 82-88)

| Literal | Value | Direction | Context |
|---|---|---|---|
| 1.94384 | m/s→kt | Condition trigger: airspeed |
| 3.28084 | m→ft | Condition trigger: altitude |
| 196.85 | m/s→fpm | Condition trigger: VS |

### PositionPanel.jsx (lines 5-8)

| Constant | Value | Direction | Context |
|---|---|---|---|
| DEG2RAD | pi/180 | deg→rad | Runway heading |
| FT2M | 0.3048 | ft→m | Altitude input |
| KT2MS | 0.514444 | kt→m/s | Airspeed input |
| NM_TO_DEG_LAT | 1/60 | NM→deg lat | Position offset |

### JSBSimFuelWriteback.cpp (line 12)

| Constant | Value | Direction | Context |
|---|---|---|---|
| KG_TO_LBS | 1/0.453592 | kg→lbs | Fuel tank writeback |

---

## 3. Conversion chains

**Altitude:**
JSBSim(ft) → JSBSimAdapter(*FT_TO_M) → FlightModelState.altitude_msl_m(m)
→ ios_backend(*3.28084) → WS alt_ft_msl(ft) → frontend display(ft)

**Airspeed (IAS):**
JSBSim(kts) → JSBSimAdapter(*KTS_TO_MS) → FlightModelState.ias_ms(m/s)
→ ios_backend(*1.94384) → WS ias_kt(kt) → frontend display(kt)

**Vertical speed:**
JSBSim(fps) → JSBSimAdapter(*FT_TO_M) → FlightModelState.vertical_speed_ms(m/s)
→ ios_backend(*196.85) → WS vs_fpm(fpm) → frontend display(fpm)

**Lat/lon:**
JSBSim(rad) → JSBSimAdapter(passthrough) → FlightModelState(rad)
→ ios_backend(*180/pi) → WS(deg) → frontend display(deg)
→ cigi_bridge(*RAD_TO_DEG) → Entity Control(deg) → X-Plane

**Fuel tank quantities:**
JSBSim(lbs) → JSBSimAdapter(*LBS_TO_KG) → FlightModelState.fuel_tank_kg(kg)
→ fuel_node(passthrough) → FuelState.tank_quantity_kg(kg)
→ fuel_writeback(*KG_TO_LBS) → JSBSim(lbs)

**Engine EGT:**
JSBSim(degF) → JSBSimAdapter((F-32)*5/9) → FlightModelState.engine_egt_degc(degC)
+ JSBSimAdapter(degC+273.15) → FlightModelState.itt_k(K) [SI backfill]

**Oil pressure:**
JSBSim(psi) → JSBSimAdapter(passthrough) → FlightModelState.engine_oil_pressure_psi(psi)
+ JSBSimAdapter(*PSI_TO_PA) → FlightModelState.oil_pressure_pa(Pa) [SI backfill]
→ engines_model(*PSI_TO_KPA) → EngineState.oil_press_kpa(kPa)
→ ios_backend(*0.145038) → WS(psi) → frontend display(psi)

**Terrain elevation (reposition):**
X-Plane probe(m MSL) → CIGI HOT Response(m) → cigi_bridge → HatHotResponse.hot(m)
→ FMA refine_terrain_altitude(/FT_TO_M) → JSBSim terrain-elevation-asl-ft(ft)

**Airport elevation:**
apt.dat(ft) → AirportDatabase(*FT2M) → Airport.elevation_m(m)
→ GetRunways service → frontend → PositionPanel(*3.28084 display, *FT2M for IC alt_m)

---

## 4. Potential issues

### Resolved by lat/lon degrees refactor (2026-03-24)
- All ROS2 lat/lon fields now use degrees. JSBSimAdapter is the single rad→deg boundary.
- HatHotResponse fields renamed: lat_deg, lon_deg, hat_m, hot_m.
- Oil pressure triple conversion: deferred (correct but wasteful).

### Confirmed issues (remaining)

1. **`_pct` suffix inconsistency** — fuel_total_pct, gear_position_pct, pitot_ice_pct store 0.0-1.0 ratios but suffix suggests 0-100%. Convention should be documented: `_pct` = ratio 0-1 in this codebase.

2. **HatHotResponse fields lack unit suffix** — `lat`, `lon`, `hat`, `hot` have no suffix. Should be `lat_rad`, `lon_rad`, `hat_m`, `hot_m`.

3. **Hardcoded literals in failures_node.cpp** — Uses `1.94384`, `3.28084`, `196.85` as raw numbers (lines 82-88) instead of named constants. Only file that does this.

4. **Oil pressure triple conversion** — JSBSim(psi) → FMS.oil_pressure_pa(Pa) → EngineState.oil_press_kpa(kPa) → WS(psi). The value starts and ends in PSI but gets converted 3 times. The instrument field (engine_oil_pressure_psi) avoids this.

5. **AvionicsControls.obs1/obs2 missing suffix** — Should be `obs1_deg`, `obs2_deg`.

6. **ElectricalState fields lack unit suffix** — bus_voltages, source_voltages (volts), source_currents, load_currents (amps) have no suffix.

7. **FlightControls/EngineControls normalized fields** — aileron, elevator, throttle[], mixture[] etc. are all normalized (-1 to 1 or 0 to 1) but have no suffix indicating this. Convention should be `_norm`.

8. **NavigationState uses _deg for GPS lat/lon** — gps1_lat_deg, gps1_lon_deg are in degrees while FlightModelState uses _rad for the same coordinates. Intentional (instrument output vs wire format) but creates inconsistency.

### Not an issue

- Mixed SI + instrument units in FlightModelState: intentional dual representation for different consumers. Documented in CLAUDE.md.
- `_rads` suffix (radians/second): technically unambiguous, just uncommon.

---

## 5. Message field inventory

### FlightModelState.msg — 80+ fields
SI primary (m, m/s, rad, Pa, K, kg, kg/s) with instrument backfills (_psi, _degc, _inhg).
Ambiguous: sim_time_sec (no suffix), load_factor_* (dimensionless), mach_number, fuel_total_pct (0-1 ratio), sim_clock.

### FuelState.msg — 25 fields
kg, litres, lph, kgs, Pa, pct(0-1). All suffixed except: density_kg_per_liter (descriptive name).

### EngineState.msg — 35+ fields
kPa, degC, inhg, kgph, kw, rpm, pct, nm, deg. Ambiguous: epr (dimensionless), vibration_level.

### ElectricalState.msg — 15 fields
**Most fields lack suffix.** bus_voltages, source_voltages, source_currents, load_currents, total_load_amps, master_bus_voltage — all missing _v/_a suffix.

### AirDataState.msg — 12 fields per system (×3)
ms, m, K, pct(0-1). Ambiguous: mach (dimensionless), pitot_ice_pct (0-1 not 0-100).

### GearState.msg — 10 fields
pct(0-1), deg. Ambiguous: brake_left/right, position_pct (0-1).

### NavigationState.msg — 50+ fields
deg, nm, kt, mhz, khz, dots. Ambiguous: signal_strength, adf_signal, transponder_code.

### AvionicsControls.msg — 15 fields
mhz, khz. Ambiguous: obs1/obs2 (should be _deg), transponder_code, tacan_channel.

### AtmosphereState.msg — 8 fields
K, Pa, kgm3, ms, m. All properly suffixed.

### WeatherState.msg — 10 fields
deg, kts, m, Pa, K. Ambiguous: turbulence_intensity (0-1), cloud_coverage (enum).

### InitialConditions.msg — 11 fields
rad, m, ms, Pa, K, pct(0-1). All suffixed. fuel_total_pct is 0-1.

### HatHotResponse.msg — 5 fields
**Worst offender.** lat, lon, hat, hot all lack unit suffix.

### FlightControls.msg — 12 fields
**All normalized, no suffix.** aileron, elevator, rudder, collective, trims, brakes, flaps, spoilers, speed_brake.

### EngineControls.msg — 5 fields
**All normalized, no suffix.** throttle[], mixture[], condition[], prop_rpm[].
