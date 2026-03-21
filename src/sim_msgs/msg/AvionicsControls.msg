# AvionicsControls.msg — Arbitrated avionics controls (authoritative)
# Published by: input_arbitrator at 50 Hz
# Consumed by: navigation_node, navaid_sim, sim_engine_systems, ios_backend,
#              and any node that needs to know what is currently tuned
# Topic: /sim/controls/avionics
#
# Single source of truth for all avionics selections.
# Matches RawAvionicsControls field-for-field plus constants.

std_msgs/Header header

# ── COM radios ──────────────────────────────────────────────────────────────
float32 com1_freq_mhz
float32 com2_freq_mhz
float32 com3_freq_mhz              # 0 = not installed

# ── NAV radios ──────────────────────────────────────────────────────────────
float32 nav1_freq_mhz
float32 nav2_freq_mhz

# ── OBS selectors ──────────────────────────────────────────────────────────
float32 obs1
float32 obs2

# ── ADF receivers ──────────────────────────────────────────────────────────
float32 adf1_freq_khz
float32 adf2_freq_khz              # 0 = not installed

# ── Transponder ────────────────────────────────────────────────────────────
uint16 transponder_code
uint8 transponder_mode
uint8 TRANSPONDER_OFF  = 0
uint8 TRANSPONDER_STBY = 1
uint8 TRANSPONDER_ON   = 2
uint8 TRANSPONDER_ALT  = 3

# ── DME source selector ───────────────────────────────────────────────────
uint8 dme_source
uint8 DME_SOURCE_NAV1 = 0
uint8 DME_SOURCE_HOLD = 1
uint8 DME_SOURCE_NAV2 = 2

# ── TACAN ──────────────────────────────────────────────────────────────────
uint16 tacan_channel               # 0 = not installed
uint8 tacan_band
uint8 TACAN_BAND_X = 0
uint8 TACAN_BAND_Y = 1

# ── GPS source selector ───────────────────────────────────────────────────
uint8 gps_source
uint8 GPS_SOURCE_GPS1 = 0
uint8 GPS_SOURCE_GPS2 = 1
