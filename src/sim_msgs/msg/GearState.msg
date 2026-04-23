# GearState — published by sim_gear on /sim/gear/state at 50Hz
# Covers both fixed-gear and retractable-gear aircraft

std_msgs/Header header

# Configuration (from gear.yaml)
uint8 gear_count                    # number of gear legs (typically 3)
string gear_type                    # "fixed_tricycle", "retractable_tricycle", "tailwheel", etc.
bool retractable                    # true if gear can retract

# Per-leg state (up to 5 legs)
string[5] leg_names                 # e.g. ["nose", "left_main", "right_main", "", ""]
float32[5] position_norm            # 0.0=retracted, 1.0=extended (always 1.0 for fixed gear)
bool[5] weight_on_wheels            # ground contact per leg (from FlightModelState)
uint8[5] status                     # 0=UP, 1=DOWN_LOCKED, 2=IN_TRANSIT, 3=FAULT

# Status constants
uint8 STATUS_UP = 0
uint8 STATUS_DOWN_LOCKED = 1
uint8 STATUS_IN_TRANSIT = 2
uint8 STATUS_FAULT = 3

# Aggregate state
bool on_ground                      # any leg has WoW (from FlightModelState)
bool gear_handle_down               # gear lever position (from FlightControls)
bool gear_unsafe                    # any leg not matching commanded position
bool gear_warning                   # warning horn active (gear unsafe + conditions met)

# Brakes (echoed from FlightControls for single-source display)
float32 brake_left_norm             # 0 to +1
float32 brake_right_norm            # 0 to +1
bool parking_brake                  # parking brake set

# Nosewheel
float32 nosewheel_angle_deg         # steering angle in degrees (from FlightModelState or computed)

# Debug — effective ground friction applied to FDM, echoed from
# FlightModelState for convenient plotting alongside WoW/brakes.
# 1.0 = dry-pavement baseline.
float32 effective_static_friction    1.0
float32 effective_rolling_friction   1.0
