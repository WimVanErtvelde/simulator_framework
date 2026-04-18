# CIGI 3.3 HAT/HOT response from Image Generator
# Published by: cigi_bridge -> /sim/cigi/hat_responses
# hat = height above terrain (m), hot = height of terrain MSL (m)

# Framework surface type enum (IG maps its surface classification to this):
#  0 = UNKNOWN       (default, treat as asphalt)
#  1 = ASPHALT       (runway, taxiway)
#  2 = CONCRETE      (runway, taxiway, ramp)
#  3 = GRASS         (grass strip, grass beside runway)
#  4 = DIRT          (unpaved)
#  5 = GRAVEL        (gravel strip)
#  6 = WATER         (lake, sea — no friction)
#  7 = SNOW_COVERED  (snow on ground — distinct from runway contamination)
#  8 = ICE_SURFACE   (frozen lake, glacier)
#  9 = SAND          (desert)
# 10 = MARSH         (soft ground)
# 11-15 = reserved
uint8   SURFACE_UNKNOWN      = 0
uint8   SURFACE_ASPHALT      = 1
uint8   SURFACE_CONCRETE     = 2
uint8   SURFACE_GRASS        = 3
uint8   SURFACE_DIRT         = 4
uint8   SURFACE_GRAVEL       = 5
uint8   SURFACE_WATER        = 6
uint8   SURFACE_SNOW_COVERED = 7
uint8   SURFACE_ICE_SURFACE  = 8
uint8   SURFACE_SAND         = 9
uint8   SURFACE_MARSH        = 10

uint32  request_id
string  point_name        # "nose", "left_main", "right_main"
float64 lat_deg
float64 lon_deg
float64 hat_m             # height above terrain (metres)
float64 hot_m             # height of terrain MSL (metres)
bool    valid

uint8   surface_type              0   # framework surface enum (see constants above)
float32 static_friction_factor    1.0 # IG-provided surface friction (0-1, default 1.0)
float32 rolling_friction_factor   1.0 # IG-provided surface friction (0-1, default 1.0)
