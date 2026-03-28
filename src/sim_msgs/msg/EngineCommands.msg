# EngineCommands.msg
# Engine write-back commands from sim_engine_systems to flight model adapter.
# Published by: sim_engine_systems -> /sim/engines/commands at 50 Hz
# For piston/turboshaft all fields are zero/false — consumers ignore unused fields.

std_msgs/Header header

float32[4] prop_pitch_deg         # Commanded prop pitch — turboprop beta range
float32[4] n1_target_pct         # Commanded N1 target — FADEC when modelled here
float32[4] fuel_flow_command_kgs  # Commanded fuel flow — FADEC when modelled here
bool[4]    feather_command        # Command prop feather
bool[4]    reverse_command        # Command reverse thrust
bool[4]    starter_engage         # Command starter motor — gated by bus voltage in engines plugin
uint8      engine_count           # Number of active engines
