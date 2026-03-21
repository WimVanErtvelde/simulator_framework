# NavigationState.msg — Onboard navigation receiver outputs
# Published by: sim_navigation at 10 Hz
# Consumed by: ios_backend, cigi_bridge, and any node needing instrument-ready nav data
# Topic: /sim/navigation/state
#
# Instrument-ready values for cockpit display and CIGI.
# Derived from AvionicsControls (tuned frequencies, OBS settings)
# + NavSignalTable (received signals from navaid_sim).
# Contains computed outputs (CDI dots, TO/FROM, relative bearings, valid flags),
# NOT raw tuning inputs. Frequencies echoed back for IOS display convenience.
# Types: float64 for lat/lon, float32 for everything else, bool for flags, string for idents.

std_msgs/Header header

# ── GPS1 (primary) ─────────────────────────────────────────────────────────
bool gps1_valid
float64 gps1_lat_deg
float64 gps1_lon_deg
float32 gps1_alt_ft
float32 gps1_gs_kt
float32 gps1_track_deg

# ── GPS2 (secondary) ───────────────────────────────────────────────────────
bool gps2_valid
float64 gps2_lat_deg
float64 gps2_lon_deg
float32 gps2_alt_ft
float32 gps2_gs_kt
float32 gps2_track_deg

# GPS source selector echo
uint8 active_gps_source            # 0=GPS1, 1=GPS2

# ── NAV1 receiver ──────────────────────────────────────────────────────────
bool nav1_valid
string nav1_ident
uint8 nav1_type                    # 0=NONE 1=VOR 2=VORDME 3=ILS 4=LOC
float32 nav1_obs_deg               # selected OBS course
float32 nav1_cdi_dots              # CDI deflection (-2.5 to +2.5)
float32 nav1_bearing_deg           # magnetic bearing TO station
float32 nav1_radial_deg            # current radial FROM station
float32 nav1_distance_nm           # DME distance (0 if no DME paired)
float32 nav1_signal_strength       # 0.0-1.0
uint8 nav1_to_from                 # 0=OFF 1=TO 2=FROM
bool nav1_gs_valid
float32 nav1_gs_dots               # GS deviation (-2.5 to +2.5)

# ── NAV2 receiver ──────────────────────────────────────────────────────────
bool nav2_valid
string nav2_ident
uint8 nav2_type
float32 nav2_obs_deg
float32 nav2_cdi_dots
float32 nav2_bearing_deg
float32 nav2_radial_deg
float32 nav2_distance_nm
float32 nav2_signal_strength
uint8 nav2_to_from
bool nav2_gs_valid
float32 nav2_gs_dots

# ── ADF1 receiver ──────────────────────────────────────────────────────────
bool adf1_valid
string adf1_ident
float32 adf1_relative_bearing_deg  # relative from aircraft nose (0=ahead, +CW)
float32 adf1_signal

# ── ADF2 receiver ──────────────────────────────────────────────────────────
bool adf2_valid
string adf2_ident
float32 adf2_relative_bearing_deg
float32 adf2_signal

# ── DME ────────────────────────────────────────────────────────────────────
uint8 dme_source                   # 0=NAV1, 1=HOLD, 2=NAV2 (echo of selector)
bool dme_valid
float32 dme_distance_nm
float32 dme_gs_kt                  # DME-derived groundspeed

# ── TACAN ──────────────────────────────────────────────────────────────────
bool tacan_valid
string tacan_ident
float32 tacan_bearing_deg
float32 tacan_distance_nm

# ── Marker beacons ─────────────────────────────────────────────────────────
bool marker_outer
bool marker_middle
bool marker_inner

# ── Transponder ────────────────────────────────────────────────────────────
uint16 transponder_code
uint8 transponder_mode             # 0=OFF 1=STBY 2=ON 3=ALT
bool transponder_ident_active      # IDENT button pressed

# ── Frequency echoes (for IOS display convenience) ─────────────────────────
float32 com1_freq_mhz
float32 com2_freq_mhz
float32 com3_freq_mhz              # 0 = not installed
float32 adf1_freq_khz
float32 adf2_freq_khz              # 0 = not installed
