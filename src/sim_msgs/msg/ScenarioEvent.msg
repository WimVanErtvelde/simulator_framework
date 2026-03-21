# ScenarioEvent.msg
# Published by: sim_manager -> /sim/scenario/event
# Fired when a timed scenario event becomes due

std_msgs/Header header
string event_id
string action            # "inject_failure", "set_weather", "set_ic"
string params_json       # action-specific parameters as JSON
