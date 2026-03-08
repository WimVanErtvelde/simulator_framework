# SimCommand.msg
# Published by: ios_backend -> /sim/command
# Consumed by: sim_manager

uint8 CMD_RUN            = 0
uint8 CMD_FREEZE         = 1
uint8 CMD_UNFREEZE       = 2
uint8 CMD_RESET          = 3
uint8 CMD_LOAD_SCENARIO  = 4
uint8 CMD_SHUTDOWN       = 5
uint8 CMD_SET_IC         = 6

std_msgs/Header header
uint8 command            # one of the CMD_* constants above
string payload_json      # optional JSON string for commands that carry data
