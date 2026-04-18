# Raw flight control inputs from devices
# Published on /devices/{hardware,virtual,instructor}/controls/flight
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
bool    parking_brake      # toggle: true = set, false = released
