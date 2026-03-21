# FailureState.msg
# Published by: sim_failures -> /sim/failure_state
# Rate: 10 Hz
# Consumers: ios_backend (IOS FAIL tab display)

builtin_interfaces/Time stamp

# Currently active (injected) failure IDs
string[] active_failure_ids

# Armed (pending trigger) failure IDs
string[] armed_failure_ids

# Per-armed-failure time remaining before injection (seconds)
# -1.0 = condition-based trigger (no countdown)
float32[] armed_trigger_remaining_s

# Nav receivers flagged as failed (e.g. "NAV1", "NAV2", "ADF")
# Handled internally by sim_failures — navigation_node reads this
string[] failed_nav_receivers

# Instruments flagged as failed (e.g. "ATT", "ALT1")
# Handled internally by sim_failures — IOS display layer reads this
string[] failed_instruments
