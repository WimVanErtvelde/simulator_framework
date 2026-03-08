# NavSignalTable.msg
# Published by: nav_sim (external service)  ->  /sim/world/nav_signals
# Receivable navaids at current position with signal strength and LOS status.
# Consumed by: navigation_node (onboard receiver simulation)
#
# NOTE: interface preliminary — will be finalised when nav_sim project is ready

std_msgs/Header header

# Tuned frequencies (set by avionics controls)
float64 nav1_freq_mhz
float64 nav2_freq_mhz
float64 adf_freq_khz

# NAV1 receiver output
bool nav1_valid
string nav1_ident
uint8 nav1_type                 # 0=none 1=VOR 2=VORDME 3=ILS 4=LOC
float64 nav1_bearing_rad        # magnetic bearing to station
float64 nav1_radial_rad         # current radial (FROM station)
float64 nav1_deviation_dots     # CDI deviation (-2.5 to +2.5 dots)
float64 nav1_distance_m         # DME distance (0 if no DME)
float64 nav1_signal_strength    # 0.0-1.0 normalised
bool nav1_gs_valid              # glideslope available
float64 nav1_gs_deviation_dots  # GS deviation (-2.5 to +2.5 dots)

# NAV2 receiver output
bool nav2_valid
string nav2_ident
uint8 nav2_type
float64 nav2_bearing_rad
float64 nav2_radial_rad
float64 nav2_deviation_dots
float64 nav2_distance_m
float64 nav2_signal_strength
bool nav2_gs_valid
float64 nav2_gs_deviation_dots

# ADF receiver output
bool adf_valid
string adf_ident
float64 adf_bearing_rad         # relative bearing to NDB
float64 adf_signal_strength

# Marker beacons
bool marker_outer
bool marker_middle
bool marker_inner

# DME hold (if tuned independently)
bool dme_valid
float64 dme_distance_m
float64 dme_ground_speed_kts
