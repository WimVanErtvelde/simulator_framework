# Arbitrated flight controls (authoritative)
# Published by input_arbitrator on /sim/controls/flight at 50 Hz
std_msgs/Header header
float64 aileron_norm       # -1 to +1
float64 elevator_norm      # -1 to +1
float64 rudder_norm        # -1 to +1
float64 collective_norm    # 0 to +1
float64 trim_aileron_norm  # -1 to +1
float64 trim_elevator_norm # -1 to +1
float64 trim_rudder_norm   # -1 to +1
float32 brake_left_norm    # 0 to +1
float32 brake_right_norm   # 0 to +1

# Secondary fixed-wing
float32 flaps_norm         # 0 to +1
float32 spoilers_norm      # 0 to +1
float32 speed_brake_norm   # 0 to +1
bool    gear_down          # true = gear extended

# Ground
bool    parking_brake

# Rotary-specific
bool    rotor_brake
