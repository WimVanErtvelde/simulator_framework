# SimCommand.msg
# Published by: ios_backend -> /sim/command
# Consumed by: sim_manager

uint8 CMD_RUN            = 0
uint8 CMD_FREEZE         = 1
uint8 CMD_UNFREEZE       = 2
uint8 CMD_RESET          = 3
uint8 CMD_LOAD_SCENARIO  = 4
uint8 CMD_SHUTDOWN       = 5
uint8 CMD_SET_IC         = 6   # update stored IC only — no reposition triggered
uint8 CMD_RELOAD_NODE      = 7
uint8 CMD_DEACTIVATE_NODE  = 8   # payload_json: {"node_name": "sim_fuel"}
uint8 CMD_ACTIVATE_NODE    = 9   # payload_json: {"node_name": "sim_fuel"}
uint8 CMD_RESET_NODE       = 10  # payload_json: {"node_name": "sim_fuel"}
                                 # chains: deactivate->cleanup->configure->activate
uint8 CMD_REPOSITION       = 11  # payload_json: IC fields — triggers FROZEN + terrain wait + return to prev state
uint8 CMD_FREEZE_POSITION  = 12  # toggle: hold lat/lon/alt while sim keeps running
uint8 CMD_FREEZE_FUEL      = 13  # toggle: hold fuel quantities constant

std_msgs/Header header
uint8 command            # one of the CMD_* constants above
string payload_json      # optional JSON string for commands that carry data
