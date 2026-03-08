# Simulator state machine
uint8 INIT=0
uint8 READY=1
uint8 RUNNING=2
uint8 PAUSED=3
uint8 REPLAY=4
uint8 FROZEN=5

uint8 state
bool freeze_position
bool freeze_fuel
bool freeze_attitude
