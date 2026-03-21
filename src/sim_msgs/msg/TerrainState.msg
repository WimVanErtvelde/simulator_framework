# TerrainState.msg
# Published by: terrain_service (TBD)  ->  /sim/world/terrain
# DTED-derived elevation and proximity data at aircraft position.
# Consumed by: GPWS logic, CIGI bridge, QTG

std_msgs/Header header

# Terrain elevation directly below aircraft
float64 elevation_msl_m         # terrain elevation (m MSL)
float64 agl_m                   # aircraft AGL (computed: alt_msl - elevation)

# Terrain gradient (for GPWS terrain awareness)
float64 terrain_slope_deg       # terrain slope in direction of travel
