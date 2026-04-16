# WeatherWindLayer.msg — sub-message for wind layers in WeatherState
# Maps to CIGI 3.3 Weather Control packet (0x0C) wind fields
# and XPLMWeatherInfoWinds_t structure

float32 altitude_msl_m           # center altitude of wind layer, MSL (m)
float32 wind_speed_ms            # horizontal wind speed (m/s)
float32 wind_direction_deg       # direction wind blows FROM, true north (deg)
float32 vertical_wind_ms         # vertical wind component (m/s, + = updraft)
float32 gust_speed_ms            # gust total speed, not delta above sustained (m/s)
float32 shear_direction_deg      # shear arc direction (deg)
float32 shear_speed_ms           # shear arc speed (m/s)
float32 turbulence_severity      # 0.0-1.0 normalized (maps to CIGI severity 0-5, XP 0-10)
