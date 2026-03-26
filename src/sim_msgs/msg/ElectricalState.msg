# Electrical system state
# Published by: sim_electrical -> /sim/electrical/state
# Rate: 50 Hz (matches solver rate)

std_msgs/Header header

# Per-bus state
string[]  bus_names
float32[] bus_voltages_v
bool[]    bus_powered

# Per-source state
string[]  source_names
bool[]    source_active
float32[] source_voltages_v
float32[] source_currents_a

# Per-load state
string[]  load_names
bool[]    load_powered
float32[] load_currents_a

# Per-switch state (contactors, relays, master switches)
string[]  switch_ids
string[]  switch_labels
bool[]    switch_closed

# Circuit breakers
string[]  cb_names
bool[]    cb_closed
bool[]    cb_tripped

# Summary fields
float32 total_load_a
float32 battery_soc_pct
float32 master_bus_voltage_v
bool    avionics_bus_powered
bool    essential_bus_powered
