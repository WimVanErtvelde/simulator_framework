# Arbitrated engine controls (authoritative)
# Published by input_arbitrator on /sim/controls/engine at 50 Hz
std_msgs/Header header
float64[] throttle
float64[] mixture
float64[] condition
float64[] prop_rpm
bool[] magneto_left
bool[] magneto_right
bool starter
