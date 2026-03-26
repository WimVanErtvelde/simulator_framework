# FlightModelState.msg
# Published by: flight_model_adapter  →  /sim/flight_model/state
# Rate: matches flight model execution rate (typically 100 Hz)
# Consumer nodes: sim_manager, cigi_bridge, navigation_node,
#                 atmosphere_node (reads wind only), gear_node, ios_backend
#
# Reference sources:
#   [HLSIM]   STAGE Helisim 6.0 Real-Time Data Export ICD (Engenuity doc 743-0507)
#   [JSBSim]  JSBSim FGPropagate.cpp / FGAuxiliary.cpp property bindings
#             https://jsbsim-team.github.io/jsbsim/
#   [XPLANE]  X-Plane datarefs sim/flightmodel/position/ namespace
#             https://developer.x-plane.com/datarefs/
#   [DIS]     IEEE 1278.1 Distributed Interactive Simulation - Entity State PDU
#   [RPR]     SISO-STD-001-2015 Real-time Platform Reference FOM v2.0
#             https://www.sisostandards.org/
#   [ARINC]   ARINC 429 label assignments (label 203 = baro altitude, etc.)
#
# Unit conventions (SI throughout, angles in radians unless noted):
#   Distance     metres (m)
#   Velocity     metres/second (m/s)
#   Acceleration metres/second^2 (m/s^2)
#   Angles       radians (rad)  — Euler angles and AoA/beta
#   Angular rate radians/second (rad/s)
#   Pressure     Pascals (Pa)
#   Temperature  Kelvin (K)
#   Mass         kilograms (kg)
#   Density      kg/m^3
#   RPM          revolutions/minute (rpm)  — rotor/engine convention
#   Torque       Newton-metres (N*m)
#
# Coordinate frames used (consistent with JSBSim and DIS/RPR FOM):
#   ECG  Earth-Centred Geodetic (lat/lon/alt)  [HLSIM word 30-35, DIS position]
#   ECEF Earth-Centred Earth-Fixed cartesian   [JSBSim FGPropagate vLocation]
#   NED  North-East-Down local tangent plane   [HLSIM word 39-41, JSBSim vVel]
#   Body Fuselage axes: X=nose, Y=right, Z=down [HLSIM word 42-44, JSBSim UVW]
#
# Section layout:
#   1. HEADER & METADATA          — all aircraft
#   2. POSITION                   — all aircraft
#   3. ORIENTATION                — all aircraft
#   4. VELOCITY                   — all aircraft
#   5. ANGULAR RATES              — all aircraft
#   6. ACCELERATION               — all aircraft
#   7. AIR DATA & ATMOSPHERE      — all aircraft
#   8. ENGINES                    — all aircraft
#   9. FUEL & MASS                — all aircraft
#  10. LANDING GEAR               — all aircraft
#  11. FLIGHT STATE               — all aircraft
#  12. AUTOPILOT / FLIGHT DIRECTOR— all aircraft (some modes heli-specific)
#  13. FIXED-WING CONTROL SURFACES— is_helicopter = false only
#  14. HELICOPTER ROTOR & DRIVE   — is_helicopter = true only
#  15. INTEROP / SYNC             — DIS/HLA, multi-channel

# ═══════════════════════════════════════════════════════════════════════════════
#  COMMON — ALL AIRCRAFT TYPES
# ═══════════════════════════════════════════════════════════════════════════════

# ─────────────────────────────────────────────────────────────────────────────
# 1. HEADER & METADATA
# ─────────────────────────────────────────────────────────────────────────────

# ROS2 standard header — carries sim-time stamp via /clock
std_msgs/Header header

# Simulation wall-clock time in seconds (separate from ROS stamp)
# Useful for QTG replay correlation.
# [HLSIM word 3] [JSBSim simulation/sim-time-sec]
float64 sim_time_sec

# Identifies which flight model produced this state (e.g. "jsbsim", "helisim", "custom")
string flight_model_name

# Aircraft type string loaded in flight model (e.g. "c172", "ec135")
string aircraft_id

# Capability flags — consumers use these to know which optional fields are valid
bool is_helicopter          # rotor_* fields valid when true
bool has_multi_engine       # engines[1..N-1] valid when true
bool has_retractable_gear   # gear[] retraction valid; otherwise gear always down

# Flight model capability flags (set by adapter, read by system nodes)
bool cap_models_fuel_quantities     # tank contents + capacity come from flight model
bool cap_models_fuel_pump_pressure  # pump failure affects fuel delivery pressure
bool cap_models_fuel_crossfeed      # crossfeed routing computed by flight model

# Flight model freeze / inhibit state
# [HLSIM word 2 bits 8-15 FreezeFlag]
bool is_frozen

# ─────────────────────────────────────────────────────────────────────────────
# 2. POSITION — ECG (geodetic) + ECEF
# Primary reference for nav, CIGI, QTG, and IOS map display.
# 64-bit doubles required for geodetic precision [DIS Entity Location uses float64]
# [HLSIM words 24-35]  [JSBSim position/lat-gc-deg, long-gc-deg, h-sl-meters]
# [XPLANE sim/flightmodel/position/latitude, longitude, elevation]
# [RPR RPR_Aircraft.WorldLocation]
# ─────────────────────────────────────────────────────────────────────────────

# Geodetic
float64 latitude_deg        # geodetic latitude (positive North, degrees)
float64 longitude_deg       # geodetic longitude (positive East, degrees)
float64 altitude_msl_m      # altitude above mean sea level
float64 altitude_agl_m      # altitude above ground level (terrain)
                             # [JSBSim position/h-agl-ft converted]
                             # [XPLANE sim/flightmodel/position/y_agl]

# ECEF cartesian — required for CIGI IG positioning and DIS/HLA interop
# [HLSIM words 24-29]  [JSBSim FGPropagate::GetLocation() -> ECEF]
# [DIS Entity Location: X,Y,Z in ECEF metres]
float64 ecef_x_m
float64 ecef_y_m
float64 ecef_z_m

# ─────────────────────────────────────────────────────────────────────────────
# 3. ORIENTATION
# Quaternion is the primary representation (numerically stable, no gimbal lock).
# Euler angles derived for display/CIGI use only.
# Rotation convention: body frame relative to local NED frame.
# [JSBSim FGPropagate::VehicleState::qAttitudeLocal (NED->Body quaternion)]
# [XPLANE sim/flightmodel/position/q[4] — scalar-first w,x,y,z]
# [RPR SpatialStruct Orientation — Psi/Theta/Phi Euler]
# ─────────────────────────────────────────────────────────────────────────────

# Quaternion NED->Body (scalar-first convention: w,x,y,z)
# [JSBSim GetQuaternion(), X-Plane q[0..3]]
float64 q_w
float64 q_x
float64 q_y
float64 q_z

# Euler angles derived from quaternion (Yaw-Pitch-Roll / ZYX convention)
# [HLSIM word 127=roll, 128=pitch, 129=true_heading]
# [JSBSim position/psi-deg (heading), position/theta-deg (pitch), position/phi-deg (roll)]
# [XPLANE sim/flightmodel/position/psi, theta, phi]
float64 roll_rad            # phi — positive right wing down
float64 pitch_rad           # theta — positive nose up
float64 true_heading_rad    # psi — true heading, 0=North, clockwise
float64 magnetic_heading_rad # magnetic heading [HLSIM word 130]

# ─────────────────────────────────────────────────────────────────────────────
# 4. VELOCITY — three frames
# All three frames needed: NED for instruments, Body for aero, ECEF for CIGI/DIS
# [HLSIM words 36-44]  [JSBSim velocities/h-dot-fps, u-fps, v-fps, w-fps]
# [XPLANE sim/flightmodel/position/local_vx/vy/vz (NED), P/Q/R body rates]
# ─────────────────────────────────────────────────────────────────────────────

# NED frame (North-East-Down) — used by navigation, QTG, IOS ground track
# [HLSIM words 39-41]  [JSBSim velocities/ned-velocity-mag-fps + components]
float64 vel_north_ms
float64 vel_east_ms
float64 vel_down_ms

# Body frame (U=axial/forward, V=lateral/right, W=normal/down)
# [HLSIM words 42-44]  [JSBSim velocities/u-fps, v-fps, w-fps]
float64 vel_u_ms            # axial (forward) body velocity
float64 vel_v_ms            # lateral (right) body velocity
float64 vel_w_ms            # normal (down) body velocity

# ECEF frame — needed for DIS Entity State PDU dead-reckoning and CIGI
# [HLSIM words 36-38]  [JSBSim FGPropagate::GetECEFVelocity()]
# [DIS Entity Linear Velocity — ECEF frame]
float64 vel_ecef_x_ms
float64 vel_ecef_y_ms
float64 vel_ecef_z_ms

# ─────────────────────────────────────────────────────────────────────────────
# 5. ANGULAR RATES — body frame (P, Q, R)
# [HLSIM words 131-133]  [JSBSim velocities/p-rad_sec, q-rad_sec, r-rad_sec]
# [XPLANE sim/flightmodel/position/P, Q, R in deg/s -> converted to rad/s]
# [DIS Dead Reckoning angular velocity]
# ─────────────────────────────────────────────────────────────────────────────

float64 roll_rate_rads      # P — roll rate (positive right)
float64 pitch_rate_rads     # Q — pitch rate (positive nose-up)
float64 yaw_rate_rads       # R — yaw rate (positive nose-right)

# ─────────────────────────────────────────────────────────────────────────────
# 6. ACCELERATION — body frame + load factors
# [HLSIM words 51-53]  [JSBSim accelerations/a-pilot-x-ft_sec2 etc.]
# [XPLANE sim/flightmodel/position/local_ax/ay/az]
# [JSBSim FGAuxiliary: Nx, Ny, Nz load factors]
# ─────────────────────────────────────────────────────────────────────────────

float64 accel_x_ms2         # body X (forward) acceleration
float64 accel_y_ms2         # body Y (right) acceleration
float64 accel_z_ms2         # body Z (down) acceleration

# Load factors (g units, 1.0 = 1g)
# [JSBSim accelerations/Nx, Ny, Nz]  [XPLANE sim/flightmodel/misc/gload]
float64 load_factor_x       # longitudinal (Nx)
float64 load_factor_y       # lateral (Ny)
float64 load_factor_z       # normal (Nz, 1.0 in level flight)

# ─────────────────────────────────────────────────────────────────────────────
# 7. AIR DATA & ATMOSPHERE
# These overlap with /sim/world/atmosphere but are included here because
# the flight model computes them internally from state + environment.
# nav_node and instruments use these flight-model-computed values (not raw atmosphere).
# [HLSIM words 54-61, 118-126]
# [JSBSim FGAuxiliary: Mach, qbar, alpha, beta, VCAS, VEAS, TAS]
# [ARINC 429 label 203 = baro altitude, label 206 = computed airspeed]
# ─────────────────────────────────────────────────────────────────────────────

# Airspeeds
float64 ias_ms              # indicated airspeed [HLSIM word 118] [ARINC label 206]
float64 tas_ms              # true airspeed      [HLSIM word 119]
float64 cas_ms              # calibrated airspeed [JSBSim aero/vcas-fps]
float64 eas_ms              # equivalent airspeed [JSBSim aero/veas-fps]
float64 ground_speed_ms     # ground speed        [HLSIM word 121]
float64 ground_track_rad    # ground track (true) [HLSIM word 122]
float64 vertical_speed_ms   # vertical speed (+up) [HLSIM word 120]
float64 mach_number         # Mach number [HLSIM word 55] [JSBSim aero/mach]

# Aero angles
float64 alpha_rad           # angle of attack     [HLSIM word 125] [JSBSim aero/alpha-deg]
float64 beta_rad            # sideslip angle      [HLSIM word 126] [JSBSim aero/beta-deg]
float64 alpha_dot_rads      # AoA rate [JSBSim aero/alphadot-deg_sec]
float64 beta_dot_rads       # sideslip rate [JSBSim aero/betadot-deg_sec]

# Altitudes
float64 baro_altitude_m     # barometric altitude [HLSIM word 123] [ARINC label 203]
float64 radar_altitude_m    # radar/radio altimeter [HLSIM word 124]

# Pressures and atmosphere
float64 dynamic_pressure_pa # qbar [HLSIM word 58] [JSBSim aero/qbar-pa]
float64 static_pressure_pa  # [HLSIM word 57]
float64 air_density_kgm3    # [HLSIM word 54]
float64 temperature_k       # outside air temperature [HLSIM word 56]

# Wind — as experienced by aircraft (flight model computed)
# [HLSIM words 59-61]  [JSBSim atmosphere/wind-north-fps etc.]
float64 wind_speed_ms       # total wind speed
float64 wind_direction_rad  # wind direction (true, from which wind blows)
float64 wind_vertical_ms    # vertical wind component

# ─────────────────────────────────────────────────────────────────────────────
# 8. ENGINES (up to MAX_ENGINES = 4)
# Array indexed [0..N-1], N = engine_count
# Turboshaft fields populated for helicopter; turbofan/piston for fixed-wing.
# [HLSIM words 62-110 — 3 engines x 16 words each, plus main shaft at 111-112]
# [JSBSim propulsion/engine[N]/... properties]
# ─────────────────────────────────────────────────────────────────────────────

uint8 engine_count          # number of active engines

# Per-engine data (fixed array, use engine_count to determine validity)
# Turboshaft specific: HPR=High Pressure Rotor, LPR=Low Pressure Rotor
float32[4] throttle_pct         # throttle lever position 0-100% [HLSIM word 63/79/95]
float32[4] fuel_flow_kgs        # fuel flow rate (kg/s) [HLSIM word 64/80/96]
float32[4] n1_pct               # N1 / LPR / gas generator speed % [HLSIM pct_lpr]
float32[4] n2_pct               # N2 / HPR / high pressure spool speed % [HLSIM pct_hpr]
float32[4] n1_rpm               # N1 absolute RPM [HLSIM word 67/83/99]
float32[4] n2_rpm               # N2 absolute RPM [HLSIM word 66/82/98]
float32[4] power_turbine_rpm    # Power turbine (Np/NR) RPM [HLSIM word 68/84/100]
float32[4] power_turbine_pct    # Power turbine % [HLSIM word 71/87/103]
float32[4] torque_nm            # Engine torque (N*m) [HLSIM word 72/88/104]
float32[4] torque_pct           # Engine torque % [HLSIM word 73/89/105]
float32[4] iat_k                # Inlet Air Temperature (K) [HLSIM IAT words 74/90/106]
float32[4] itt_k                # Inter-Turbine Temperature (K) [HLSIM ITT words 75/91/107]
float32[4] oil_pressure_pa      # Oil pressure (Pa) [HLSIM word 77/93/109]
float32[4] oil_temperature_k    # Oil temperature (K) [HLSIM word 78/94/110]

# Instrument-unit engine fields (populated by adapter, used by engine_systems plugins)
# These duplicate some SI fields above in instrument-ready units to avoid
# per-frame conversion in every consumer.
float32[4] engine_egt_degc              # Exhaust Gas Temperature (°C) [JSBSim egt-degR]
float32[4] engine_oil_pressure_psi      # Oil pressure (PSI) [JSBSim oil-pressure-psi]
float32[4] engine_oil_temp_degc         # Oil temperature (°C) [JSBSim oil-temperature-degK]
float32[4] engine_cht_degc              # Cylinder Head Temperature (°C, piston only) [JSBSim cylinder-head-temp-degF]
float32[4] engine_manifold_pressure_inhg # Manifold pressure (inHg, piston only) [JSBSim manifest-pressure-inhg]

# Engine status flags — bitmask per engine
# Bit 0: running, Bit 1: on fire, Bit 2: failed, Bit 3: starting
uint8[4] engine_status_flags

# ─────────────────────────────────────────────────────────────────────────────
# 9. FUEL & MASS
# [HLSIM words 134-138]  [JSBSim propulsion/total-fuel-lbs etc.]
# ─────────────────────────────────────────────────────────────────────────────

float32 fuel_total_kg           # total fuel mass on board [HLSIM word 135]
float32 fuel_total_norm         # fuel mass ratio 0.0-1.0 [HLSIM word 136]
float32[8] fuel_tank_kg         # per-tank fuel mass [HLSIM words 137-138 + extended]
float32[8] fuel_tank_capacity_kg  # per-tank capacity from flight model
uint8 fuel_tank_count               # number of tanks reported by flight model

# [HLSIM word 134 total mass]  [JSBSim inertia/mass-slugs converted]
float32 total_mass_kg           # total aircraft mass (airframe + fuel + payload)

# ─────────────────────────────────────────────────────────────────────────────
# 10. LANDING GEAR (up to 5 gear assemblies)
# [HLSIM words 139-153]  [JSBSim contact/unit[N]/... properties]
# [XPLANE sim/flightmodel/forces/fside_gear etc.]
# ─────────────────────────────────────────────────────────────────────────────

uint8 gear_count                # number of gear assemblies

# Gear position 0.0 = fully retracted, 1.0 = fully extended [HLSIM words 141-145]
float32[5] gear_position_norm
# Wheel steering/caster angle [HLSIM words 146-150]
float32[5] wheel_angle_deg
# Ground contact flags [HLSIM word 151, bits per gear]
bool[5] gear_on_ground
# Gear status: 0=up, 1=down, 2=in-transit, 3=fault [HLSIM words 152-153]
uint8[5] gear_status

# ─────────────────────────────────────────────────────────────────────────────
# 11. FLIGHT STATE FLAGS
# ─────────────────────────────────────────────────────────────────────────────

bool on_ground                  # at least one gear compressed [HLSIM word 139 bit 0]
bool crashed                    # Flight model crash detection active [HLSIM word 117]
uint8 flight_state              # 0=parked 1=taxi 2=takeoff 3=climb
                                # 4=cruise 5=descent 6=approach 7=landing
                                # [HLSIM word 167 bits 8-15]

# ─────────────────────────────────────────────────────────────────────────────
# 12. AUTOPILOT / FLIGHT DIRECTOR
# Used by IOS to display AP engagement status and FD bars.
# [HLSIM words 165-171]  [JSBSim autopilot/ properties]
# ─────────────────────────────────────────────────────────────────────────────

bool autopilot_engaged          # [HLSIM word 165 bit 0]
bool flight_director_on         # [HLSIM word 165 bit 1]

# AP mode codes (0=off)
# Pitch: 0=off 1=ALT 2=VS 3=GS 4=go-around [HLSIM word 166 bits 0-7]
uint8 ap_pitch_mode
# Roll: 0=off 1=HDG 2=VOR 3=LOC 4=LNAV    [HLSIM word 166 bits 8-15]
uint8 ap_roll_mode
# Yaw:  0=off 1=heading-hold 2=coordinated [HLSIM word 166 bits 16-23]
uint8 ap_yaw_mode
# Arm modes [HLSIM word 167]
uint8 ap_pitch_arm_mode
uint8 ap_roll_arm_mode

# AP reference/commanded values
float32 cmd_pitch_rad           # [HLSIM word 168]
float32 cmd_roll_rad            # [HLSIM word 169]
float32 cmd_heading_rad         # [HLSIM word 170]
float32 cmd_vertical_speed_ms   # [HLSIM word 171]

# ═══════════════════════════════════════════════════════════════════════════════
#  FIXED-WING — valid when is_helicopter = false
# ═══════════════════════════════════════════════════════════════════════════════

# ─────────────────────────────────────────────────────────────────────────────
# 13. CONTROL SURFACES
# [JSBSim fcs/ properties]
# [XPLANE sim/flightmodel/controls/ aileron, elevator, rudder deflections]
# ─────────────────────────────────────────────────────────────────────────────

float32 aileron_left_deg        # left aileron deflection (+trailing edge down)
float32 aileron_right_deg       # right aileron deflection
float32 elevator_deg            # elevator deflection (+nose up)
float32 rudder_deg              # rudder deflection (+nose right)
float32 flap_pct                # flap deployment 0-100% [XPLANE flaprqst]
float32 speed_brake_pct         # speed brake/spoiler deployment 0-100%
float32 aileron_trim_deg        # aileron trim [JSBSim fcs/aileron-trim-cmd-norm]
float32 elevator_trim_deg       # elevator trim [JSBSim fcs/elevator-trim-cmd-norm]
float32 rudder_trim_deg         # rudder trim

# ═══════════════════════════════════════════════════════════════════════════════
#  HELICOPTER — valid when is_helicopter = true
# ═══════════════════════════════════════════════════════════════════════════════

# ─────────────────────────────────────────────────────────────────────────────
# 14. ROTOR & DRIVETRAIN
# [HLSIM words 111-116, 154-158]
# ─────────────────────────────────────────────────────────────────────────────

# Main rotor drivetrain
float32 main_shaft_torque_nm    # torque on main rotor shaft [HLSIM word 111]
float32 main_shaft_torque_pct   # % of limit [HLSIM word 112]

# Rotor arrays [0]=main rotor, [1]=tail rotor (or second main for coaxial/tandem)
float32[2] rotor_rpm            # rotor angular velocity (RPM) [HLSIM words 115-116]
float32[2] rotor_accel_rads2    # rotor angular acceleration (rad/s^2) [HLSIM words 113-114]

# Rotor blade controls
# [HLSIM words 154-158]  [JSBSim fcs/collective-cmd-norm etc.]
float32 cyclic_lateral_deg      # lateral cyclic trim blade pitch (deg) [HLSIM word 154]
float32 cyclic_longitudinal_deg # longitudinal cyclic trim blade pitch (deg) [HLSIM word 155]
float32 pedal_trim_deg          # pedal/tail rotor trim blade pitch (deg) [HLSIM word 156]
float32 collective_deg          # collective blade pitch (deg) [HLSIM word 157]
float32 collective_pct          # collective as % [HLSIM word 158]

# Helicopter-specific autopilot modes
bool sas_engaged                # Stability Augmentation System [HLSIM word 165 bit 2]
bool collective_control_engaged # [HLSIM word 165 bit 3]
# Collective: 0=off 1=ALT 2=VS             [HLSIM word 166 bits 24-31]
uint8 ap_collective_mode

# ═══════════════════════════════════════════════════════════════════════════════
#  INTEROP / MULTI-CHANNEL SYNC
# ═══════════════════════════════════════════════════════════════════════════════

# ─────────────────────────────────────────────────────────────────────────────
# 15. SIMULATION CLOCK
# For multi-channel synchronisation and recording correlation.
# [HLSIM word 266 SimClock]
# ─────────────────────────────────────────────────────────────────────────────

float64 sim_clock               # Flight model internal clock (may differ from sim_time_sec
                                # after freeze/reposition)
