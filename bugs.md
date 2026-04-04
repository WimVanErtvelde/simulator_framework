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

### Bug #9: failures_node publisher not activated
- FailureInjection messages to routing topics silently dropped
- Console: "publisher is not activated" despite node state ACTIVE (3)
- ROOT CAUSE: 5 routing LifecyclePublishers created in on_configure() but
  missing explicit on_activate() calls. All other system nodes had this
  pattern — failures_node was the outlier.
- FIX: Added on_activate() calls for all 5 routing publishers.
- NOTE: Secondary issue — failures.yaml target IDs (ALT1, BAT1, CB_FUEL_PUMP)
  didn't match v2 graph element IDs (alternator, battery, cb_fuel_pump).
  Updated failures.yaml component_ids to match graph node/connection IDs.

### Bug #8: FORCE checkbox return path incomplete
- AircraftPanel.jsx used localForced (React local state) instead of forcedSwitchIds from Zustand store
- The full pipeline existed: input_arbitrator → ArbitrationState → ios_backend WS → useSimStore
- FIX: Replaced `isForced(id)` to check `forcedSwitchIds.includes(id)` from store. Removed localForced state and setLocalForced calls.

## Cleanup

### Dead wiring cleanup (2026-04-03)
- Removed 5 dead `/sim/failures/active` subscriptions (FailureList — no publisher existed)
- Removed `/sim/failure/navaid_commands` publisher from failures_node (ground station failures are world conditions, not aircraft equipment failures — will use `/sim/world/navaid_command` path)
- Removed dead `_ic_pub` (InitialConditions publisher) from ios_backend (created but never called)
- `/ios/failure_command` already renamed to `/devices/instructor/failure_command` (was done before this cleanup)
- Old ElectricalSolver (elec_sys namespace) deleted — GraphSolver (elec_graph) is the only solver
- EC135 electrical plugin stubbed to no-op pending v2 YAML migration

### Architecture audit batch (2026-04-04) — all already resolved
- F4.1: JSBSimAdapter gear clear pos-norm — Already correct: `active ? 0.0 : 1.0` (clears to 1.0)
- F3.6: fuel_node reset on RESETTING — Already correct: `model_->reset()` called on state transition
- F3.3: on_deactivate lifecycle publisher — Already correct: all 4 nodes (electrical, engines, gear, air_data) call `state_pub_->on_deactivate()`
- F3.8: electrical solver state gating — By design: INIT/READY intentionally run solver so cockpit has bus power before CMD_RUN
- F4.10: atmosphere density uses ISA temp — Already correct: uses `oat_k` (ISA + deviation), not ISA baseline

### Deferred (by design)
- F3.7: DME HOLD reset on RESETTING — Not a bug. DME HOLD is an avionics-device-specific feature (e.g., KNS-80 panel function), not universal receiver behavior. navigation_node is the aircraft-agnostic receiver layer and should not own avionics device state. When the avionics plugin layer is built, DME HOLD state will migrate there. Stale DME HOLD across sim reset is cosmetic and not training-critical.

## Open

(none)
