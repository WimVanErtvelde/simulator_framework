# SimAlert.msg
# Published by: sim_manager -> /sim/alerts
# Severity levels for operational alerts

uint8 SEVERITY_INFO     = 0
uint8 SEVERITY_WARNING  = 1
uint8 SEVERITY_CRITICAL = 2

std_msgs/Header header
uint8 severity           # one of the SEVERITY_* constants above
string source            # node name that triggered the alert
string message           # human-readable alert description
