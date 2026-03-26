# RawAvionicsControls.msg — Raw avionics input from a single device source
# Published by: microros_bridge, virtual_panels, ios_backend
# Consumed by: input_arbitrator (ONLY)
# Topics: /devices/{hardware,virtual,instructor}/controls/avionics
#
# Contains ALL possible avionics controls any device could send.
# Must match AvionicsControls.msg field-for-field (minus constants).

std_msgs/Header header

# ── COM radios ──────────────────────────────────────────────────────────────
float32 com1_freq_mhz
float32 com2_freq_mhz
float32 com3_freq_mhz              # 0 = not installed

# ── NAV radios ──────────────────────────────────────────────────────────────
float32 nav1_freq_mhz
float32 nav2_freq_mhz

# ── OBS selectors ──────────────────────────────────────────────────────────
float32 obs1_deg
float32 obs2_deg

# ── ADF receivers ──────────────────────────────────────────────────────────
float32 adf1_freq_khz
float32 adf2_freq_khz              # 0 = not installed

# ── Transponder ────────────────────────────────────────────────────────────
uint16 transponder_code
uint8 transponder_mode

# ── DME source selector ───────────────────────────────────────────────────
uint8 dme_source                   # 0=NAV1, 1=HOLD, 2=NAV2

# ── TACAN ──────────────────────────────────────────────────────────────────
uint16 tacan_channel               # 0 = not installed
uint8 tacan_band                   # 0=X, 1=Y

# ── GPS source selector ───────────────────────────────────────────────────
uint8 gps_source                   # 0=GPS1, 1=GPS2
