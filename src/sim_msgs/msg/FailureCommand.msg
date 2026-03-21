# FailureCommand.msg
# Published by: ios_backend -> /ios/failure_command
# Consumed by: sim_failures
#
# Actions: "inject", "arm", "clear", "clear_all"
# Trigger modes (for arm): "delay", "condition"

string action
string failure_id
string trigger_mode
float32 trigger_delay_s
string condition_param
string condition_operator
float32 condition_value
float32 condition_duration_s
string params_override_json    # if non-empty, overrides catalog params at inject time
