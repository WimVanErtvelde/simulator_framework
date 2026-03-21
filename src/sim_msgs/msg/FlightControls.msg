# Arbitrated flight controls (authoritative)
# Published by input_arbitrator on /sim/controls/flight at 50 Hz
std_msgs/Header header
float64 aileron
float64 elevator
float64 rudder
float64 collective
float64 trim_aileron
float64 trim_elevator
float64 trim_rudder
float32 brake_left    # 0.0–1.0
float32 brake_right   # 0.0–1.0

# Secondary fixed-wing
float32 flaps          # 0.0–1.0 normalised
float32 spoilers       # 0.0–1.0
float32 speed_brake    # 0.0–1.0
bool    gear_down      # true = gear extended

# Ground
bool    parking_brake

# Rotary-specific
bool    rotor_brake
