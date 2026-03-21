# EngineState.msg
# Published by: sim_engine_systems -> /sim/engines/state
# Rate: 50 Hz
# Cockpit systems layer output — instrument-ready engine data.
# Per-engine fixed arrays (max 4 engines), indexed [0..engine_count-1].
# Zeros for unused fields — consumers check engine_count.

std_msgs/Header header

uint8 engine_count
string engine_type

# Engine state machine constants
uint8 STATE_OFF=0
uint8 STATE_CRANKING=1
uint8 STATE_STARTING=2
uint8 STATE_RUNNING=3
uint8 STATE_SHUTDOWN=4
uint8 STATE_FAILED=5

uint8[4] engine_state           # EngineRunState per engine

# Core parameters
float32[4] n1_pct
float32[4] n2_pct
float32[4] engine_rpm

# Temperatures — technically different measurement points
float32[4] itt_degc             # Inter-Turbine Temperature
float32[4] egt_degc             # Exhaust Gas Temperature
float32[4] tot_degc             # Turbine Outlet Temperature
float32[4] cht_degc             # Cylinder Head Temperature (piston)

# Pressures & flow
float32[4] oil_press_kpa
float32[4] oil_temp_degc
float32[4] fuel_flow_kgph
float32[4] manifold_press_inhg

# Power output
float32[4] torque_nm
float32[4] torque_pct
float32[4] shp_kw

# Status flags
bool[4] starter_engaged
bool[4] generator_online

# Drivetrain — scalar, not per-engine (driven by ALL engines through gearbox)
float32 prop_rpm
float32 main_rotor_rpm
float32 tail_rotor_rpm

# Warning flags (threshold-driven from aircraft YAML)
bool[4] low_oil_pressure_warning
bool[4] high_egt_warning
bool[4] high_cht_warning

# FADEC mode per engine (NORM/TOGA/FLEX/IDLE/REV/OFF)
string fadec_mode_0
string fadec_mode_1
string fadec_mode_2
string fadec_mode_3

# Extended type-specific fields (additive — zeroed for types that don't use them)
float32[4] epr                  # Engine Pressure Ratio — turbojet/turbofan
float32[4] beta_deg             # Propeller blade angle — turboprop beta range
bool[4]    feather_state        # Prop feathered — turboprop
bool[4]    reverse_active       # Reverse thrust active — turboprop/turbofan
float32[4] vibration_level      # Engine vibration 0-1 — turbofan health monitoring
float32[4] boost_pressure_inhg  # Turbocharger boost — turbocharged piston
float32[4] intercooler_temp_degc # Intercooler temp — turbocharged diesel piston
