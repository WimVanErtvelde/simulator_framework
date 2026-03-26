# Fuel system state
# Published by: sim_fuel -> /sim/fuel/state at 50 Hz
std_msgs/Header header

# Tank metadata (from fuel.yaml config, constant per aircraft)
uint8 tank_count                      # actual number of tanks (use to slice arrays)
float32 density_kg_per_liter          # fuel density at ISA (AVGAS 0.72, Jet-A1 0.80)
string fuel_type                      # "AVGAS_100LL", "JET_A1", etc.

# Per-tank quantities (slots 0..tank_count-1 valid, rest zeroed)
float32[8] tank_quantity_kg
float32[8] tank_quantity_norm       # 0.0-1.0 ratio (mass / capacity)
float32[8] tank_usable_kg
float32[8] tank_quantity_liters       # = tank_quantity_kg / density_kg_per_liter
float32[8] tank_flow_out_lph          # Flow out of each tank outlet (L/h). Zeroed by
                                      # current flat solver. Populated by future graph
                                      # solver. Consumers can subscribe now.

# Totals
float32    total_fuel_kg
float32    total_fuel_norm          # 0.0-1.0 ratio (total mass / total capacity)
float32    total_fuel_liters          # = total_fuel_kg / density_kg_per_liter

# Per-engine fuel flow (single source of truth for all consumers)
float32[4] engine_fuel_flow_kgs       # kg/s from flight model
float32[4] engine_fuel_flow_lph       # liters/hour = kgs / density * 3600

# System state
float32[8] fuel_pressure_pa
bool[4]    boost_pump_on
bool[8]    tank_selected
bool       low_fuel_warning
float32    cg_contribution_m
