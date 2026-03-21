# Per-channel arbitration state
# Published by input_arbitrator on /sim/controls/arbitration at 10 Hz

uint8 SOURCE_FROZEN     = 0
uint8 SOURCE_HARDWARE   = 1
uint8 SOURCE_VIRTUAL    = 2
uint8 SOURCE_INSTRUCTOR = 3

std_msgs/Header header
uint8 flight_source       # one of the SOURCE_* constants
uint8 engine_source
uint8 avionics_source
uint8 panel_source
bool  hardware_flight_healthy
bool  hardware_engine_healthy
bool  hardware_avionics_healthy
bool  hardware_panel_healthy
