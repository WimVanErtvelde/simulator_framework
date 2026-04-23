# WeatherPatch.msg — localized weather region authored by IOS instructor.
#
# Maps to CIGI 3.3:
#   - Environmental Region Control packet (0x0B, ID=11): one per patch
#     (circular: SizeX=SizeY=0, CornerRadius=radius_m)
#   - Weather Control packets (0x0C) with Scope=Regional referencing patch_id:
#     one per cloud layer, wind layer, and any enabled scalar override
#
# Maps to X-Plane SDK (xplanecigi plugin):
#   - XPLMSetWeatherAtLocation(lat, lon, XPLMWeatherInfo_t)
#   - radius_nm     = radius_m * 0.000539957
#   - max_altitude_msl_ft = auto-derived from max cloud/wind altitude + 2000 ft buffer
#                           (XPLM_DEFAULT_WXR_LIMIT_MSL_FT = 10000 ft if no layers)
#
# Patch ID allocation: monotonic uint16 owned by ios_backend, reset on session
# start or IOS restart. No reuse within a session.

uint16  patch_id                   # CIGI Region ID (1-based; 0 reserved)

string  patch_type                 # "airport" | "custom"
string  label                      # Display label (IOS tab title, CIGI debug)
string  icao                       # "EBBR" for patch_type="airport"; "" for custom

float64 lat_deg                    # Patch center latitude
float64 lon_deg                    # Patch center longitude
float32 radius_m                   # Horizontal radius; bounds 1852.0 (1 nm) - 92600.0 (50 nm)
                                   # Default 5556.0 (3 nm) for custom, 18520.0 (10 nm) for airport

# ── Ground elevation reference ─────────────────────────────────────────
# Populated by ios_backend on patch create / lat-lon change:
#   - patch_type="airport": ARP elevation from airport DB (navaid_sim
#     SearchAirports service) via ICAO lookup
#   - patch_type="custom":  SRTM terrain elevation from navaid_sim
#     /navaid_sim/get_terrain_elevation service at patch lat/lon
# Used by ios/frontend for live AGL/MSL display on cloud and wind altitude
# sliders. NOT transmitted over CIGI (framework-to-IG wire uses MSL only;
# plugin derives its own ground reference via XPLMProbeTerrainXYZ at patch
# lat/lon). Set to NaN until populated.
float64 ground_elevation_m

# ── Layer overrides ──────────────────────────────────────────────────────
# Empty array = no override for that layer type (global layers apply within patch)
sim_msgs/WeatherCloudLayer[] cloud_layers
sim_msgs/WeatherWindLayer[]  wind_layers

# ── Scalar overrides ─────────────────────────────────────────────────────
# Each override_X flag gates the corresponding value field(s).
# When false, the global WeatherState value applies within the patch.

bool    override_visibility
float32 visibility_m               # Only applied when override_visibility=true

bool    override_temperature
float32 temperature_k              # Only applied when override_temperature=true

bool    override_precipitation
float32 precipitation_rate         # 0.0-1.0 intensity; applied when override_precipitation=true
uint8   precipitation_type         # 0=None, 1=Rain, 2=Snow, 3=Sleet
                                   # Applied when override_precipitation=true

bool    override_humidity
uint8   humidity_pct               # 0-100; applied when override_humidity=true
                                   # Wire-only today (no FDM consumer); reserved for
                                   # future cockpit hygrometer + CIGI regional moisture

bool    override_pressure
float32 pressure_sl_pa             # 80000-110000 Pa; applied when override_pressure=true
                                   # Altimeter setting only (qnh_pa) — does NOT shift
                                   # P_isa or density (field QNH exercise, not density physics)

bool    override_runway
uint8   runway_condition_idx       # 0-15 category × severity index (Dry=0,
                                   # Wet 1-3, Standing Water 4-6, Snow 7-9,
                                   # Ice 10-12, Snow+Ice 13-15). Indexes the
                                   # per-aircraft contamination_factors table
                                   # — not a friction coefficient itself.
                                   # Applied when override_runway=true AND
                                   # aircraft on ground inside patch.
