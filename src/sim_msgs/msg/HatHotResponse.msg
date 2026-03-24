# CIGI 3.3 HAT/HOT response from Image Generator
# Published by: cigi_bridge -> /sim/cigi/hat_responses
# hat = height above terrain (m), hot = height of terrain MSL (m)

uint32  request_id
string  point_name        # "nose", "left_main", "right_main"
float64 lat_rad
float64 lon_rad
float64 hat_m             # height above terrain (metres)
float64 hot_m             # height of terrain MSL (metres)
bool    valid
