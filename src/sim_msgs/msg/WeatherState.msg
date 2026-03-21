# WeatherState.msg
# Published by: sim_manager  ->  /sim/world/weather
# Injected met environment from IOS or scenario scripts.
# Consumed by: atmosphere_node, cigi_bridge, ice_protection_node

std_msgs/Header header

# Wind
float64 wind_direction_deg      # direction wind is blowing FROM (true north)
float64 wind_speed_kts          # sustained wind speed (knots)
float64 wind_gust_kts           # gust speed above sustained (knots)
float64 turbulence_intensity    # 0.0 = none, 1.0 = severe

# Visibility and cloud
float64 visibility_m            # horizontal visibility (metres)
float64 cloud_base_ft           # cloud base altitude AGL (feet)
uint8 cloud_coverage            # 0=CLR, 1=FEW, 2=SCT, 3=BKN, 4=OVC

# Icing conditions
bool visible_moisture             # true = in cloud or precipitation (instructor-set)

# Temperature and pressure deviations
float64 oat_deviation_k         # deviation from ISA temperature (K), 0 = ISA standard
float64 qnh_pa                  # altimeter setting (Pa), 101325 = ISA standard
