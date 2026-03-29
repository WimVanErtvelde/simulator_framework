# bugs.md — Known Issues

## Resolved

### Bug #1: Engine cranks without battery power
- Starter engaged regardless of electrical bus state
- FIX: Starter routed through EngineCommands writeback. Engines plugin checks bus_voltage > 20V AND magneto START before setting starter_engage.

### Bug #2: Battery shows 21V instead of 24V nominal
- SOC was hardcoded instead of read from YAML initial_soc
- FIX: Initial SOC loaded from electrical.yaml battery.initial_soc

### Bug #3: Alternator never outputs 28V
- n1_pct was always 0 for piston engines — alternator RPM gate never passed
- FIX: JSBSimAdapter populates n1_pct = propeller_rpm / max_rpm * 100

### Bug #4: Sticky instructor priority locks out cockpit
- Any IOS panel command locked out ALL cockpit switches until node reconfigure
- FIX: Per-switch FORCE model. has_inst_panel_ removed. Each switch tracks force independently.

### Bug #5: Loads draw current without switch
- Loads with switch_id drew current even when switch was off
- FIX: ElectricalSolver gates load current on panel_switch_states_ for loads with switch_id

### Bug #6: Essential bus stays dead (0V)
- sw_ess_bus_tie relay never closed — updateRelayCoils() only opened on unpowered coil
- FIX: ss.closed = coil_powered (energized = closed, de-energized = open). commandSwitch() skips relay-type switches.

### Bug #7: COM1 never draws current
- nominal_current: 25 (typo, should be 2.5) caused instant CB trip (25A > 5A * 1.3)
- FIX: Corrected to 2.5 in electrical.yaml

## Open

### Bug #8: FORCE checkbox return path incomplete
- forcedSwitchIds from ArbitrationState not reliably reaching frontend store
- WORKAROUND: Local React state (localForced) in AircraftPanel.jsx bypasses round-trip
- IMPACT: Visual only — force/release commands work correctly via WS
