# AtmosphereState.msg
# Published by: atmosphere_node  ->  /sim/world/atmosphere
# ISA baseline + weather deviations at current aircraft position.
# Consumed by: engines_node, ice_protection_node, altimeter logic, QTG

std_msgs/Header header

# ISA-derived values at current altitude
float64 pressure_pa             # static pressure (Pa)
float64 temperature_k           # outside air temperature (K)
float64 density_kgm3            # air density (kg/m^3)
float64 speed_of_sound_ms       # local speed of sound (m/s)

# Altitude references
float64 pressure_altitude_m     # altitude from ISA pressure (m)
float64 density_altitude_m      # altitude from ISA density (m)

# Deviations from ISA (instructor-injected via /sim/world/weather)
float64 isa_temperature_dev_k   # delta from ISA temperature (K)
float64 qnh_pa                  # altimeter setting (Pa), ISA = 101325
