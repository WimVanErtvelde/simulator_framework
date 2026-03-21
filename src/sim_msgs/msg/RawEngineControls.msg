# Raw engine control inputs from devices
# Published on /devices/{hardware,virtual,instructor}/controls/engine
std_msgs/Header header
float64[] throttle
float64[] mixture
float64[] condition
float64[] prop_rpm
bool[] magneto_left
bool[] magneto_right
bool starter
