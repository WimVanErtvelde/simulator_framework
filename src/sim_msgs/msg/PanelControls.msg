# Panel switch and selector state
# Published on /devices/{hardware,virtual,instructor}/panel (input)
# Published on /sim/controls/panel (arbitrated output, on change only)
std_msgs/Header header
string[] switch_ids      # matches IDs in aircraft electrical.yaml
bool[]   switch_states
bool[]   switch_forced   # parallel to switch_ids: true=force, false=release (empty=normal)
string[] selector_ids    # rotary selectors e.g. sel_fuel
int32[]  selector_values
bool[]   selector_forced # parallel to selector_ids (empty=normal)
string[]  pot_ids            # panel potentiometers (analog 0.0–1.0)
float32[] pot_values
string[]  encoder_abs_ids    # absolute rotary encoders
int32[]   encoder_abs_values
string[]  encoder_rel_ids    # relative rotary encoders (delta)
int32[]   encoder_rel_deltas
