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
