# AtmosphereState.msg
# Published by: atmosphere_node  ->  /sim/world/atmosphere
# ISA baseline + weather deviations at current aircraft position.
# Consumed by: engines_node, ice_protection_node, altimeter logic, QTG

std_msgs/Header header

# ISA-derived values at current altitude (before deviations)
float64 temperature_k           # ISA temperature (K)
float64 pressure_pa             # static pressure (Pa)
float64 density_kgm3            # air density (kg/m^3)
float64 speed_of_sound_ms       # local speed of sound (m/s)

# Actual OAT with instructor deviation applied
float64 oat_k                   # outside air temperature = ISA temp + deviation (K)

# Altimeter setting
float64 qnh_pa                  # altimeter setting (Pa), ISA = 101325

# Altitude references
float64 density_altitude_m      # altitude from ISA density (m)
float64 pressure_altitude_m     # altitude from ISA pressure (m)

# Wind at aircraft position — single source of truth
# Sum of ambient + Dryden perturbation + microburst outflow
# Computed by weather_solver_node. Written to FDM via JSBSimAtmosphereWriteback.
float64 wind_north_ms            # NED north component (m/s)
float64 wind_east_ms             # NED east component (m/s)
float64 wind_down_ms             # NED down component (m/s, + = downdraft)

# Moisture and turbulence at aircraft position
bool    visible_moisture          # true = in cloud or precipitation
float32 turbulence_intensity      # 0.0-1.0 at aircraft position

# Effective runway friction at aircraft position
# Patch-aware: equals global WeatherState.runway_friction, unless aircraft is
# inside a patch with override_runway=true, in which case it's the patch value.
# Consumed by flight_model_adapter_node → JSBSimSurfaceWriteback. The "apply only
# on ground" gate lives in the writeback (on_ground state), not here.
uint8   effective_runway_friction
