# Raw flight control inputs from devices
# Published on /devices/{hardware,virtual,instructor}/controls/flight
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
