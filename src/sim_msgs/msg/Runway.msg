# Runway data — two ends per physical runway strip
string designator_end1            # "25L", "07R", "01", etc
float64 threshold_lat_rad_end1
float64 threshold_lon_rad_end1
float64 heading_deg_end1          # magnetic heading
float64 displaced_threshold_m_end1

string designator_end2
float64 threshold_lat_rad_end2
float64 threshold_lon_rad_end2
float64 heading_deg_end2
float64 displaced_threshold_m_end2

float64 width_m
float64 length_m
int32 surface_type                # 1=asphalt, 2=concrete, 3=turf, etc (XP convention)
float64 elevation_m
