# Per-channel hardware health
# Published on /devices/hardware/heartbeat by microros_bridge
std_msgs/Header header
string device_id       # "hardware", "virtual"
bool   flight_ok
bool   engine_ok
bool   avionics_ok
bool   panel_ok
