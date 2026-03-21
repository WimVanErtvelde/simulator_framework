# FailureInjection.msg
# Published by: sim_failures -> handler-specific topics
#   /sim/failure/flight_model_commands
#   /sim/failure/electrical_commands
#   /sim/failure/navaid_commands
# Consumed by: flight_model_adapter, sim_electrical, navaid_sim

string failure_id
string handler
string method
string params_json
bool active
