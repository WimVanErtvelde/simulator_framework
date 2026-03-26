# Failure Pipeline Audit

Generated: 2026-03-26

## Summary

| Aircraft | Total in catalog | Fully implemented | Bug in clear path | Partial (no consumer) | Wrong routing |
|---|---|---|---|---|---|
| C172 | 19 | 15 | 2 | 1 | 1 |
| EC135 | 21 | 15 | 2 | 2 | 2 |

**Critical bugs found:**
1. **CB clear re-pops instead of restoring** — electrical_node doesn't check `msg->active`, so clear message re-triggers the popped state
2. **EC135 gear entries use wrong method** — catalog says `set_gear_unsafe_indication` but intent is `set_gear_unable_to_extend` (different JSBSim properties)
3. **Gear unsafe clear uses -1.0 sentinel** — unclear if JSBSim treats -1.0 correctly for WoW
4. **Oil pressure clear hardcodes 60 PSI** — wrong for aircraft with different normal pressure
5. **Armed failure params_override lost** — ArmedEntry struct has no field for it, so armed navaid failure loses station_id when it fires

---

## Routing Coverage

| Route topic | Handler string | Publisher in sim_failures | Subscriber node | Callback |
|---|---|---|---|---|
| `/sim/failure/flight_model_commands` | `flight_model` | `fdm_cmd_pub_` | flight_model_adapter | `adapter_->apply_failure(method, params_json, active)` |
| `/sim/failure/electrical_commands` | `electrical` | `elec_cmd_pub_` | sim_electrical | `on_failure_injection(msg)` |
| `/sim/failure/navaid_commands` | `navaid_sim` | `navaid_cmd_pub_` | navaid_sim | `on_failure_injection(msg)` |
| `/sim/failure/air_data_commands` | `air_data` | `air_data_cmd_pub_` | sim_air_data | `model_->apply_failure(method, active)` |
| `/sim/failure/gear_commands` | — | **MISSING** | sim_gear | `model_->apply_failure(method, active)` |
| (internal) | `sim_failures` | N/A (handled in-process) | sim_navigation (via FailureState) | `is_failed(id)` lambda |

**Missing route: `gear`** — `failures_node.cpp` has no `gear_cmd_pub_`. All gear failures in both catalogs use `handler: flight_model`, routing to JSBSimAdapter instead of sim_gear. sim_gear's subscription to `/sim/failure/gear_commands` never receives anything.

**No failure subscription:** sim_engine_systems, sim_fuel (by design — engine/fuel failures are handled by JSBSim FDM directly)

---

## Per-Failure Trace — C172

### ata24_alternator_failure — Alternator Failure
- ATA: 24 | Handler: `electrical` | Method: `set_electrical_component_failed`
- Route: failures_node → `/sim/failure/electrical_commands` → sim_electrical
- Implementation: **IMPLEMENTED**
- Effect: `model_->apply_failure("ALT1/fail", true)` → solver opens ALT1 source, bus voltage drops
- Clear: `model_->apply_failure("ALT1/fail", false)` → solver restores ALT1
- Notes: Clean inject/clear cycle

### ata24_battery_failure — Battery Failure
- ATA: 24 | Handler: `electrical` | Method: `set_electrical_component_failed`
- Route: failures_node → `/sim/failure/electrical_commands` → sim_electrical
- Implementation: **IMPLEMENTED**
- Effect: `model_->apply_failure("BAT1/fail", true)` → solver removes BAT1 source
- Clear: Symmetric with `false`

### ata24_cb_fuel_pump — CB - Fuel Pump
- ATA: 24 | Handler: `electrical` | Method: `set_circuit_breaker_state`
- Route: failures_node → `/sim/failure/electrical_commands` → sim_electrical
- Implementation: **BUG — clear path broken**
- Effect: Inject sends `{component_id: "CB_FUEL_PUMP", state: "popped"}`. Callback sets `cb_overrides_[cb_id] = POPPED` and calls `model_->command_switch(cb_id, 0)` (opens CB).
- Clear: `clear_failure()` sends `active=false` but with original `params_json` containing `state: "popped"`. **`on_failure_injection()` does NOT check `msg->active`** — it branches only on `msg->method` then on `state` string. Since state is still `"popped"`, it re-pops the CB instead of clearing. The else branch (which does `cb_overrides_.erase()`) is only reachable if state is neither "popped" nor "locked".
- Notes: **Must fix.** Add `msg->active` check before CB state branch.

### ata26_engine_fire — Engine Fire
- ATA: 26 | Handler: `flight_model` | Method: `set_engine_fire`
- Route: failures_node → `/sim/failure/flight_model_commands` → JSBSimAdapter
- Implementation: **IMPLEMENTED**
- Effect: `propulsion/engine[0]/fire-now` = 1.0
- Clear: `propulsion/engine[0]/fire-now` = 0.0

### ata28_fuel_leak_left — Fuel Leak - Left Tank
- ATA: 28 | Handler: `flight_model` | Method: `set_fuel_tank_drain`
- Route: failures_node → `/sim/failure/flight_model_commands` → JSBSimAdapter
- Implementation: **IMPLEMENTED**
- Effect: `active_drains_[0] = 20.0 lph`. Applied each `step()`: reads `propulsion/tank[0]/contents-lbs`, subtracts drain amount, writes back. Fuel physically depletes.
- Clear: `active_drains_.erase(0)`. Drain stops. Already-drained fuel is not restored.

### ata28_fuel_leak_right — Fuel Leak - Right Tank
- Same as above with `tank_index: 1`
- Implementation: **IMPLEMENTED**

### ata32_gear_unsafe_indication — Gear Unsafe Indication
- ATA: 32 | Handler: `flight_model` | Method: `set_gear_unsafe_indication`
- Route: failures_node → `/sim/failure/flight_model_commands` → JSBSimAdapter
- Implementation: **IMPLEMENTED (clear uses -1.0 sentinel)**
- Effect: `gear/unit[0]/wow` = 0.0 (forces WoW false → unsafe light on ground)
- Clear: `gear/unit[0]/wow` = -1.0 (sentinel to let JSBSim recompute)
- Notes: The -1.0 sentinel may or may not work correctly depending on JSBSim model. Also: this routes to FMA, not sim_gear — sim_gear's subscription to `/sim/failure/gear_commands` never receives it.

### ata34_nav1_receiver_failed — NAV1 Receiver Failure
- ATA: 34 | Handler: `sim_failures` (internal) | Method: `set_nav_receiver_failed`
- Route: Handled internally in failures_node → `failed_nav_receivers_` set → FailureState broadcast
- Implementation: **IMPLEMENTED**
- Effect: "NAV1" added to `failed_nav_receivers`. navigation_node reads FailureState, zeros NAV1 output (valid=false, CDI=0, bearing=0, signal=0, GS=false). DME also zeroed if source is NAV1.
- Clear: "NAV1" removed from set. Navigation output restores.

### ata34_nav2_receiver_failed — NAV2 Receiver Failure
- Same pattern as NAV1 with receiver_id "NAV2"
- Implementation: **IMPLEMENTED**

### ata34_adf_receiver_failed — ADF Receiver Failure
- Same pattern with receiver_id "ADF"
- Implementation: **IMPLEMENTED**

### ata34_attitude_indicator_failed — Attitude Indicator Failure
- ATA: 34 | Handler: `sim_failures` (internal) | Method: `set_instrument_failed`
- Route: Handled internally → `failed_instruments_` set → FailureState broadcast
- Implementation: **PARTIAL — flag stored, no consumer reads it**
- Effect: "ATT" added to `failed_instruments` in FailureState. No node currently reads `failed_instruments` to alter any output. The attitude indicator on the virtual cockpit would need to read this to tumble/flag.
- Clear: "ATT" removed from set.
- Notes: Infrastructure exists, effect not implemented.

### ata34_pitot_blocked — Pitot Blocked (drain clear)
- ATA: 34 | Handler: `air_data` | Method: `pitot_blocked_drain_clear`
- Route: failures_node → `/sim/failure/air_data_commands` → sim_air_data
- Implementation: **IMPLEMENTED**
- Effect: `pitot_blocked_failure_=true`, `drain_blocked_=false`. Pitot pressure frozen at current value. IAS decays toward zero as altitude changes. `pitot_healthy` flag set false.
- Clear: `pitot_blocked_failure_=false`. `trapped_pitot_pressure_` reset to ambient. IAS recovers.

### ata34_pitot_blocked_drain — Pitot Blocked (drain blocked)
- ATA: 34 | Handler: `air_data` | Method: `pitot_blocked_drain_blocked`
- Route: failures_node → `/sim/failure/air_data_commands` → sim_air_data
- Implementation: **IMPLEMENTED**
- Effect: `pitot_blocked_failure_=true`, `drain_blocked_=true`. Trapped pressure stays at blockage value. IAS increases with climb (acts like altimeter).
- Clear: `pitot_blocked_failure_=false`. Trapped pressure NOT reset (drain was blocked). Physics gradually returns to correct as pressure equalizes.

### ata34_static_port_blocked — Static Port Blocked
- ATA: 34 | Handler: `air_data` | Method: `static_port_blocked`
- Route: failures_node → `/sim/failure/air_data_commands` → sim_air_data
- Implementation: **IMPLEMENTED**
- Effect: `static_blocked_failure_=true`. Static pressure frozen. Altitude freezes, VSI reads zero, IAS incorrect at different altitudes. Alternate static switch bypasses blockage.
- Clear: `static_blocked_failure_=false`, `trapped_static_pressure_` reset to ambient.

### ata71_engine_failure — Engine Failure
- ATA: 71 | Handler: `flight_model` | Method: `set_engine_running`
- Route: failures_node → `/sim/failure/flight_model_commands` → JSBSimAdapter
- Implementation: **IMPLEMENTED**
- Effect: `propulsion/engine[0]/set-running` = 0.0 (engine stops)
- Clear: `propulsion/engine[0]/set-running` = 1.0 (engine restarts)

### ata71_partial_power_60pct — Partial Power (60%)
- ATA: 71 | Handler: `flight_model` | Method: `set_engine_thrust_scalar`
- Implementation: **IMPLEMENTED**
- Effect: `propulsion/engine[0]/thrust-scalar` = 0.6
- Clear: `propulsion/engine[0]/thrust-scalar` = 1.0

### ata71_partial_power_80pct — Partial Power (80%)
- Same as above with value 0.8
- Implementation: **IMPLEMENTED**

### ata71_low_oil_pressure — Low Oil Pressure
- ATA: 71 | Handler: `flight_model` | Method: `set_engine_oil_pressure`
- Implementation: **IMPLEMENTED (clear hardcodes 60 PSI)**
- Effect: `propulsion/engine[0]/oil-pressure-psi` = 15.0
- Clear: `propulsion/engine[0]/oil-pressure-psi` = 60.0 (hardcoded, not from config)
- Notes: 60 PSI is correct for C172 but wrong for other aircraft.

### world_navaid_station_failed — Navaid Station Failure
- ATA: none | Handler: `navaid_sim` | Method: `set_navaid_station_failed`
- Route: failures_node → `/sim/failure/navaid_commands` → navaid_sim
- Implementation: **IMPLEMENTED**
- Effect: Station ident added to `failed_stations_` set. Signal output gated: NAV1/NAV2 valid=false + signal=0 + GS=false. ADF valid=false + signal=0. DME valid=false + distance=0 (if co-located with NAV1 VOR). Markers NOT gated.
- Clear: Ident removed from set. Signals restore on next 10 Hz update.
- Notes: Requires `params_override_json` with `station_id` from IOS. NavaidSearch panel provides this.

---

## Per-Failure Trace — EC135 (additions/differences from C172)

### ata24_alternator_failure_1 / ata24_alternator_failure_2
- Same mechanism as C172, component_id ALT1/ALT2
- Implementation: **IMPLEMENTED**

### ata24_battery_failure
- Same as C172
- Implementation: **IMPLEMENTED**

### ata26_engine_fire_1 / ata26_engine_fire_2
- Same mechanism, engine_index 0/1
- Implementation: **IMPLEMENTED**

### ata28_fuel_leak_left / ata28_fuel_leak_right
- Same as C172
- Implementation: **IMPLEMENTED**

### ata32_gear_unable_to_extend_nose — Nose Gear Unable to Extend
- ATA: 32 | Handler: `flight_model` | Method: `set_gear_unsafe_indication`
- Implementation: **BUG — wrong method name**
- Effect: Catalog says `set_gear_unsafe_indication` which sets `gear/unit[0]/wow` (WoW override). Display name says "Unable to Extend" which should use `set_gear_unable_to_extend` setting `gear/unit[0]/pos-norm` (gear position).
- Notes: Two different JSBSim properties with completely different effects. Must change method to `set_gear_unable_to_extend`.

### ata32_gear_unable_to_extend_main — Main Gear Unable to Extend
- Same bug as above with gear_index 1
- Implementation: **BUG — wrong method name**

### ata34_nav1/nav2/adf_receiver_failed
- Same as C172
- Implementation: **IMPLEMENTED**

### ata34_attitude_indicator_failed
- Same as C172
- Implementation: **PARTIAL — no consumer**

### ata34_altimeter_failed — Altimeter Failure
- ATA: 34 | Handler: `sim_failures` (internal) | Method: `set_instrument_failed`
- Implementation: **PARTIAL — no consumer**
- Effect: "ALT1" added to `failed_instruments`. No node reads this to alter altimeter output.
- Notes: EC135-only entry. Same gap as attitude indicator.

### ata34_pitot_failed — Pitot Tube Failure
- ATA: 34 | Handler: `flight_model` | Method: `set_pitot_failed`
- Route: failures_node → `/sim/failure/flight_model_commands` → JSBSimAdapter
- Implementation: **IMPLEMENTED** (if EC135 JSBSim model has `systems/pitot[0]/serviceable`)
- Effect: `systems/pitot[0]/serviceable` = 0.0
- Clear: `systems/pitot[0]/serviceable` = 1.0
- Notes: EC135 uses FDM-level pitot failure (vs C172 which uses air_data handler). Different approach, both valid.

### ata63_tail_rotor_failure — Tail Rotor Failure
- ATA: 63 | Handler: `flight_model` | Method: `set_tail_rotor_failed`
- Route: failures_node → `/sim/failure/flight_model_commands` → JSBSimAdapter
- Implementation: **IMPLEMENTED** (if EC135 JSBSim model has `systems/tail-rotor/serviceable`)
- Effect: `systems/tail-rotor/serviceable` = 0.0
- Clear: `systems/tail-rotor/serviceable` = 1.0

### ata71_engine_failure_1/2, partial_power_60pct_1/2, low_oil_pressure_1/2
- Same mechanisms as C172 with engine_index 0/1
- Implementation: **IMPLEMENTED** (oil pressure clear hardcodes 60 PSI — note for EC135)

### world_navaid_station_failed
- Same as C172
- Implementation: **IMPLEMENTED**

---

## IOS Frontend Status

| Feature | Status |
|---|---|
| Failure catalog display (ATA grouped) | Working |
| Inject button (immediate) | Working |
| Clear button | Working |
| Clear All button | Working |
| Active failure count + red styling | Working |
| Navaid station search + inject with params_override | Working |
| Armed failure UI (delay/condition config) | **NOT IMPLEMENTED** — backend supports it, no frontend UI |
| Armed failure countdown display | **NOT IMPLEMENTED** — data reaches frontend, silently dropped by store |
| `failed_nav_receivers` display | **NOT IMPLEMENTED** — data dropped by store |
| `failed_instruments` display | **NOT IMPLEMENTED** — data dropped by store |

---

## Missing Pieces

### Must Fix (bugs)

1. **CB clear re-pops** — `electrical_node.cpp` `on_failure_injection()` must check `msg->active` before branching on CB state. When `active=false`, should erase override and let solver control.

2. **EC135 gear method names** — `ec135/config/failures.yaml` entries `ata32_gear_unable_to_extend_nose` and `ata32_gear_unable_to_extend_main` must change method from `set_gear_unsafe_indication` to `set_gear_unable_to_extend`.

### Should Fix (clear-path correctness)

3. **Oil pressure clear hardcoded** — `JSBSimAdapter::apply_failure()` `set_engine_oil_pressure` clear restores to 60.0 PSI. Should save the pre-failure value on inject and restore it on clear, or read normal pressure from config.

4. **Gear WoW clear sentinel** — `set_gear_unsafe_indication` clear sets `gear/unit[N]/wow` to -1.0. Verify this is the correct sentinel for JSBSim to recompute contact state.

5. **Armed params_override lost** — `ArmedEntry` struct needs a `params_override_json` field so armed navaid failures preserve their station_id through firing.

### Should Add (incomplete features)

6. **Gear failure route** — Add `gear_cmd_pub_` to `failures_node.cpp` `route_injection()` for `handler: "gear"`. Then change C172 `ata32_gear_unsafe_indication` handler from `flight_model` to `gear` (or keep FDM route and accept sim_gear subscription is unused).

7. **Instrument failure consumers** — `failed_instruments` set in FailureState is populated but no node reads it. Virtual cockpit instrument components need to check FailureState for ATT/ALT1 flags.

8. **Frontend armed failure UI** — ARM button, delay/condition form, countdown display.

9. **Frontend FailureState fields** — Store `armed_trigger_remaining_s`, `failed_nav_receivers`, `failed_instruments` in useSimStore.js.
