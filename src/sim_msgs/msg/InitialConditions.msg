# InitialConditions.msg
# Published by: sim_manager -> /sim/initial_conditions
# Broadcast during RESETTING state so all nodes can reset to these values

std_msgs/Header header
float64 latitude_deg
float64 longitude_deg
float64 altitude_msl_m
float64 heading_rad
float64 bank_rad           # initial roll angle (positive = right bank)
float64 pitch_rad          # initial pitch angle (positive = nose up)
float64 airspeed_ms
float64 oat_deviation_k
float64 qnh_pa
float32 fuel_total_norm              # 0.0-1.0 ratio
string configuration     # "cold_and_dark", "ready_for_takeoff", "airborne_clean"
