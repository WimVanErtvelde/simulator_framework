# Arbitrated engine controls (authoritative)
# Published by input_arbitrator on /sim/controls/engine at 50 Hz
std_msgs/Header header
float64[] throttle_norm     # 0 to +1
float64[] mixture_norm      # 0 to +1
float64[] condition_norm    # 0 to +1
float64[] prop_lever_norm   # 0 to +1 (governor lever position, NOT actual RPM)
bool[] magneto_left
bool[] magneto_right
bool starter
