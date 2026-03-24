# SimState.msg
# Published by: sim_manager -> /sim/state
# Rate: 10 Hz + on every state transition

uint8 STATE_INIT      = 0
uint8 STATE_READY     = 1
uint8 STATE_RUNNING   = 2
uint8 STATE_FROZEN    = 3
uint8 STATE_RESETTING = 4
uint8 STATE_SHUTDOWN  = 5
# NOTE: STATE_REPOSITIONING (6) removed — repositioning uses FROZEN + sim_manager internal flag

std_msgs/Header header
uint8 state            # one of the STATE_* constants above
string aircraft_id     # currently loaded aircraft type
float64 sim_time_sec   # current sim clock value in seconds
float32 time_scale     # current time acceleration factor (1.0 = real-time)
bool reposition_active # true while a reposition is in progress (sim is FROZEN for terrain loading)
bool freeze_position   # true = hold lat/lon/alt while sim keeps running (engines/aero still compute)
bool freeze_fuel       # true = hold fuel quantities constant (no drain)
