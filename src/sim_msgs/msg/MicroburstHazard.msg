# MicroburstHazard.msg — Oseguera-Bowles 1988 microburst field parameters
# Consumed by weather_solver_node for FDM wind computation
# No CIGI standard packet — uses user-defined 0xCA

uint16  hazard_id                # unique ID per active microburst
float64 latitude_deg             # center position
float64 longitude_deg            # center position
float32 core_radius_m            # R parameter — radius of peak outflow
float32 shaft_altitude_m         # Z_m — center altitude of downdraft shaft
float32 intensity                # lambda parameter (m/s scale factor)
uint8   lifecycle_phase          # 0=Dormant, 1=Intensifying, 2=Mature, 3=Decaying
float32 activation_time_sec      # sim time at activation (for lifecycle computation)

# Patch linkage (Slice 5c)
# 0 = standalone (not tied to any patch)
# Nonzero = matches WeatherPatch.patch_id; cascade-deletes with patch
uint16  source_patch_id
