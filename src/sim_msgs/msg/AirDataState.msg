# AirDataState — published by sim_air_data on /sim/air_data/state at 50Hz
# Instrument air data computed from pitot-static system model.
# Consumers (IOS, cockpit displays) read this instead of FlightModelState
# for airspeed, altitude, and VSI. FlightModelState remains truth.

std_msgs/Header header

# Configuration
uint8 system_count                    # number of pitot-static systems (C172=1, A320=3)
string[3] system_names                # e.g. ["primary", "", ""] or ["captain", "first_officer", "standby"]

# Instrument values per system (up to 3)
float32[3] indicated_airspeed_ms      # IAS from pitot-static differential
float32[3] calibrated_airspeed_ms     # CAS (IAS corrected for position error)
float32[3] mach                       # derived from pitot-static ratio
float32[3] altitude_indicated_m       # pressure altitude corrected for QNH setting
float32[3] altitude_pressure_m        # raw pressure altitude (QNH = 1013.25 hPa)
float32[3] vertical_speed_ms          # rate of change of indicated altitude
float32[3] sat_k                      # static air temperature
float32[3] tat_k                      # total air temperature (SAT + ram rise)

# System health per system
bool[3] pitot_healthy                 # false = pitot blocked (icing or failure)
bool[3] static_healthy                # false = static port blocked
bool[3] pitot_heat_on                 # true = heater powered (from electrical load state)
bool[3] alternate_static_active       # true = alternate static source selected

# Icing state per system
float32[3] pitot_ice_norm             # 0.0 = clear, 1.0 = fully blocked

# Compass instrument output (aircraft-level, not per pitot-static system)
float32 magnetic_heading_rad          # Derived: true_heading - magnetic_variation
float32 magnetic_variation_deg        # East positive, from WMM via navaid_sim
