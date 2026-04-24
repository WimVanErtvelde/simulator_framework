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

### Bug #10: Forced switch remembers pilot click after unlock
- Pilot clicks on virtual cockpit switches during instructor FORCE were stored in
  input_arbitrator virtual_value. On FORCE release, stale pilot value took effect.
- FIX (3 layers):
  A) Frontend: CockpitElectrical.jsx + C172Panel.jsx guard clicks when forcedSwitchIds includes the switch
  B) input_arbitrator: discard VIRTUAL/HARDWARE input for switches/selectors where ctrl.forced == true
  C) input_arbitrator: on FORCE release, copy force_value into virtual_value and hardware_value

### Bug #11: CB only pops when a switch is toggled afterward
- SwitchControlState.virtual_value defaults to false. When pilot pulls a CB (sends false)
  for the first time, change detection (virtual_value != state) evaluates false != false
  → no change → publish_effective_panel() never runs. CB command stored but only reaches
  electrical_node when a subsequent switch toggle triggers a publish.
- ROOT CAUSE: Missing first-input detection. Arbitrator compares against uninitialized
  default instead of recognizing has_virtual false→true as itself a change.
- FIX: Set changed=true when has_virtual or has_hardware transitions false→true (first
  input from that source). Applies to both switch and selector branches.

### Bug #12: Battery never charges when alternator is online
- updateBatterySoc() only checked one-hop connections from battery node.
  Topology: battery → hot_batt_bus → sw_battery → primary_bus. hot_batt_bus
  has power_source=="battery" so charge condition never fired. Alternator-powered
  primary_bus was two hops away and unreachable.
- FIX: Replace one-hop neighbor check with mini-BFS from battery through passable
  connections. If any reachable node is powered by a non-battery source, charge.
  Respects sw_battery state (master off → no charge path → no charge).
- BUG #12b: Battery terminal voltage not updated during charging. updateSources()
  sets voltage from OCV only. When alternator is online and charging, battery
  terminal voltage should reflect the imposed charging voltage (~28V), not OCV (~24V).
  hot_batt_bus inherits the wrong voltage from BFS.
- FIX: In updateBatterySoc(), capture max voltage from charging source. Update
  battery terminal voltage to charging voltage minus IR drop. Post-charge one-hop
  re-propagation updates hot_batt_bus.
- BUG #12c: updateSources() overwrites charging voltage every frame. The charging
  voltage set by updateBatterySoc() only survived one frame before updateSources()
  clobbered it with OCV on the next step().
- FIX (reverted): Added charging_voltage to NodeState — broke buses (zero voltages).
- BUG #12c (take 2): Previous fix stored persistent state in NodeState, causing
  initialization ordering issues. New approach: detectChargingVoltage() helper reads
  previous frame's propagation results AFTER updateSources() applies failure overrides.
  If a charging source is found, battery voltage is overridden before propagate() runs.
  No persistent state in NodeState. Validates charging source is still online to prevent
  stale voltage from failed sources.
- FIX: detectChargingVoltage() BFS from battery through passable connections, checks
  that power_source node is online with voltage > 0. Battery voltage set to charging
  voltage minus IR drop after updateSources, before propagate. BFS naturally propagates
  ~28V through hot_batt_bus.
- BUG #12d: Segfault in detectChargingVoltage on startup — .at() on empty maps,
  unguarded adjacency access before topology loaded.
- FIX: Replaced .at() with .find() + null checks, added bounds checks on adjacency
  indices, guarded call site in step() with !adjacency_.empty().
- BUG #12e: Heap corruption from ABI mismatch — battery_charge_voltages_ member added
  to header changed class layout. electrical_node built against old header loaded
  graph_solver.so with new layout.
- FIX: Removed member variable, replaced with local computation in step(). Clean
  rebuild (rm build/install for sim_electrical + aircraft_c172) required.
- BUG #12f: Battery never enters charge branch because clock/ELT loads on hot_batt_bus
  give total_current > 0, forcing discharge-only branch. Charge check was in the else.
- FIX: Removed if/else split. Always compute drain AND charge. Net current (charge
  minus drain) determines SOC direction. Charge current is physics-based:
  (V_alternator - V_ocv) / R_internal, clamped to [0, charge_rate_max]. Natural
  CC/CV taper as SOC rises.

### Bug #13: Fuel slider triggers full JSBSim reset
- set_fuel_loading WS command was routed through InitialConditions msg
  with fuel_total_norm. Fuel solver IC subscription triggers JSBSim RunIC,
  resetting position, attitude, and velocities.
- FIX: Bypass IC path entirely. ios_backend publishes PayloadCommand on
  /aircraft/fuel/load_command. JSBSimAdapter subscribes and writes
  propulsion/tank[N]/contents-lbs directly. No RunIC, no reset.
  Fuel solver internal state may be stale until proper integration (deferred).
- BUG #13b: Direct JSBSim tank write bypassed fuel solver. Fuel solver
  overwrites tank contents every frame from its own internal state.
- FIX: Moved /aircraft/fuel/load_command subscription from JSBSimAdapter
  to fuel_node. Fuel solver receives command, updates internal tank state
  via IFuelModel::set_tank_quantity(). Writeback naturally pushes
  new value to JSBSim. Adapter no longer writes tank contents directly.

### Bug #14: NumpadPopup "7" touch-through
- Tapping NumpadPopup open immediately registered a "7" keypress — the
  same tap that opened the popup bled through to the newly mounted numpad.
- FIX: requestAnimationFrame mount guard — numpad ignores tap events until
  the frame after mount.

### Bug #15: Partial weather wipe on ACCEPT
- ACCEPT replaced _last_weather_data wholesale; previously set fields
  (clouds, microbursts, precip) were wiped when only atmosphere was updated.
- FIX: Backend publish_weather() does dict.update() merge instead of
  replacement. Cloud-layer writes go through dedicated add/remove WS
  messages so ACCEPT cannot touch them.

### Bug #16: Cloud layer feedback loop broken
- Frontend never saw cloud layers reflected after ACCEPT — store.activeWeather
  stayed empty despite backend logs showing the data was cached and republished.
- ROOT CAUSE: stale ament_python build artifacts. `colcon build` without
  `--symlink-install` left an older copy of ios_backend_node.py in
  install/ios_backend/.../site-packages/; the running backend imported that
  stale copy and never sent the weather_state broadcast.
- FIX: rebuild with --symlink-install. Documented as a permanent workflow
  constraint in CLAUDE.md and the session checklist.

### Bug #17: Stale cloud layers in X-Plane
- Removing a cloud layer in IOS left the old layer rendered in X-Plane.
- ROOT CAUSE: weather_encoder emitted N packets for N current cloud layers;
  xplanecigi kept slot `valid = true` for any slot that stopped receiving
  packets. Removing layer 2 meant slot 2 was never written → stale visuals.
- FIX: encoder always emits 3 cloud packets (layer_id 1, 2, 3). Missing
  layers emit weather_enable=0. xplanecigi clears the slot whenever
  weather_enable=0 is received.

### Bug #18: XP runway_friction dataref type mismatch
- XPLMSetDatai(dr_runway_friction, ...) silently failed to change the
  visual wetness/ice on the runway.
- ROOT CAUSE: sim/weather/region/runway_friction is a float dataref, not
  int. XPLMSetDatai() silently no-ops on float-typed datarefs.
- FIX: xplanecigi uses XPLMSetDataf((float)runway_friction). Cast is
  cheap; float representation of 0–15 is exact.

### Bug #19: XP surface_texture_type dataref name
- xplanecigi bound sim/flightmodel/ground/surface_type and always reported
  xp_surf=0 regardless of what the aircraft was parked on.
- ROOT CAUSE: the dataref we need is called surface_texture_type (not
  surface_type). Confirmed in DataRefTool.
- FIX: primary and fallback lookups updated to use surface_texture_type.

### Bug #20: CIGI SOF and HAT/HOT Response parsed with wrong packet IDs
- cigi_bridge parsed incoming SOF as 0x01 and HAT/HOT Response as 0x02.
  CIGI 3.3 ICD defines SOF as 0x65 (101) and HAT/HOT Response as 0x66 (102).
  Worked only because xplanecigi sent matching non-standard IDs — both sides
  had the same bug and cancelled out.
- ROOT CAUSE: RX parser in recv_pending() used bare literals lifted from the
  Host→IG opcode table (IG Control 0x01, Entity Control 0x02) instead of the
  IG→Host IDs.
- FIX: Added named constants CIGI_PKT_SOF = 0x65 and
  CIGI_PKT_HAT_HOT_EXT_RESPONSE = 0x67 in cigi_host_node.hpp (the latter
  promoted to spec-compliant Extended Response during the follow-up
  opcode audit — see DECISIONS.md 2026-04-23). recv_pending() uses the
  constants; xplanecigi emits SOF with buf[0] = 0x65 and a 40-byte
  0x67 Extended Response. Both sides must be rebuilt and redeployed
  together.

### Bug #21: CIGI Entity Control sent with wrong opcode (0x03 instead of 0x02)
- cigi_host_node.hpp defined CIGI_PKT_ENTITY_CTRL = 0x03 and encoded a full
  48-byte Entity Control payload (roll/pitch/yaw/lat/lon/alt) under that ID.
  Per CIGI 3.3 §4.1.2, Entity Control is opcode 0x02 (48 B). Opcode 0x03 is
  Conformal Clamped Entity Control (24 B, yaw+lat+lon only, requires entity
  to already exist with Ground/Ocean Clamp = Conformal). Any compliant IG
  would misparse or drop our Ownship updates.
- ROOT CAUSE: wrong constant; worked only because xplanecigi parsed incoming
  as `case 0x03:` with size 48 — symmetric bug, same cancel-out as #20.
- FIX: CIGI_PKT_ENTITY_CTRL = 0x02 in cigi_host_node.hpp; xplanecigi dispatch
  changed to `case 0x02:`. Both sides must be rebuilt and redeployed
  together, same deployment constraint as #20.

## Known Limitations

### Limitation #1: XPLMSetWeatherAtLocation produces no visible effect at training-relevant radii in X-Plane 12
- Regional weather patches (10–50 NM) emit correct CIGI Region Control + Weather Control Scope=Regional packets end-to-end; xplanecigi plugin correctly invokes `XPLMSetWeatherAtLocation` with a fully populated `XPLMWeatherInfo_t`. SDK call returns success but X-Plane's visual rendering shows no discernible localized change (visibility, clouds blended into the global field).
- Confirmed: pipeline end-to-end correct, SDK call fires with right values, no visual rendering change observed.
- Likely causes: X-Plane 12 weather engine blends samples over distances ≫ 10 NM; SDK docs also say the call is "not intended to be used per-frame" but from pre-flight loop only (we call 1 Hz from flight loop). Third-party XP12 weather plugins avoid this API for localized visual effects.
- No framework code fix. Revisit if SDK changes or customer requires localized visual weather (see DECISIONS.md 2026-04-20 entry).

### KL-1: XPLMSetWeatherAtLocation produces no visible effect at training radii

**Scope**: X-Plane 12 visual rendering
**Slice observed**: 5b (WeatherPanelV2 patches)
**Status (2026-04-24)**: **Workaround shipped in plugin** (`blend_at_ownship` mode, default for all new builds). Spec-compliant `sdk_regional` mode retained behind config flag for future SDK fix or non-XP IG integration.
**Documented in**: DECISIONS.md entries 2026-04-20 (original analysis) and 2026-04-24 (blend-at-ownship workaround).

**Symptom (original)**: Patch authored in WX2 with visibility or cloud override, aircraft positioned inside the patch radius (tested at 10 NM and 30+ NM), no visible difference from global weather.

**Verified healthy path (original)**: Authoring UI → store → WS → ios_backend → WeatherState.patches[] → cigi_bridge Region Control + Weather Control emission → xplanecigi plugin decode → XPLMSetWeatherAtLocation SDK call with correct XPLMWeatherInfo_t. Plugin log confirms each step.

**Root cause**: X-Plane 12's weather engine blends regional samples against global to invisibility at training-relevant radii. The SDK call fires correctly with valid data; X-Plane's renderer simply doesn't honour small regional samples. Third-party XP12 weather plugins (Active Sky, VisualXP) avoid this API for localized visibility/fog for the same reason.

**Workaround (`blend_at_ownship` mode, default)**: Plugin computes the effective weather at ownship from global + active patches and writes the result to the existing global `sim/weather/region/*` datarefs. Patches now render visibly when the aircraft is inside one. Visibility was confirmed working end-to-end on 2026-04-24 (EBAW patch, vis 800m, mid-flight). Trade-offs:
- Cloud regen on patch-exit is deferred to X-Plane's natural ~60s cadence (no regen scheduled because the global cloud values themselves didn't change).
- Visibility writes are immediate-apply (snap update); other writes (clouds, wind, temp, pressure) are gated by `regen_weather` mode to avoid multi-second freezes from `update_immediately` on big deltas.
- Runway-condition patch override is FDM-only on the X-Plane visual path: `sim/weather/region/runway_friction` is a single global scalar with no spatial behaviour. FDM friction differentiation between airports continues to work via `weather_solver` → `AtmosphereState`.

**Spec-compliant fallback**: `xplanecigi.ini` → `plugin_weather_mode = sdk_regional` reactivates the per-patch `XPLMSetWeatherAtLocation` path. Same behaviour as before: spec-correct CIGI traffic, no visible patch rendering. Useful when (a) Laminar fixes the SDK or (b) integrating a spec-strict non-XP IG.

**Revisit if**: Laminar fixes XP12 SDK to render small regional samples; OR a customer requires per-patch rendering with multiple aircraft (current workaround only blends at ownship — other entities still see global).

(Supersedes Limitation #1 above with richer structure; Limitation #1 retained for append-only log continuity.)

## Open

(none)
