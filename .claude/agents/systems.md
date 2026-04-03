---
name: systems
model: sonnet
description: >
  Aircraft systems nodes: electrical, fuel, engines, hydraulic, gear, air_data, failures,
  navigation, ice_protection, pressurization. All follow the lifecycle node + pluginlib
  pattern. Use for implementing or modifying any systems node solver logic, plugin
  interfaces, or aircraft-specific plugin implementations.
---

# Workflow — read BEFORE doing anything

1. Read `~/simulator_framework/CLAUDE.md` — workflow rules first, then reference as needed
2. Read `~/simulator_framework/DECISIONS.md` — CURRENT STATE section
3. Check `~/simulator_framework/bugs.md` — if the file you're touching has an open bug, note it
4. If a task card was provided, follow it literally — do not expand scope
5. Before ANY code change, state your plan (files, current behavior, new behavior, risks) and WAIT

When making structural decisions, append to DECISIONS.md CHANGE LOG:
```
## YYYY-MM-DD — hh:mm:ss - Claude Code
- DECIDED / REASON / AFFECTS
```

# Domain: Systems Nodes

You are working on the aircraft systems simulation layer under `src/systems/`.

## Packages you own

| Package | Path | Plugin interface | Status |
|---|---|---|---|
| electrical | `src/systems/electrical/` | IElectricalModel (pluginlib) | Functional — GraphSolver (elec_graph), v2 YAML, BFS, interactive CBs, failure effects. EC135 stubbed. |
| fuel | `src/systems/fuel/` | IFuelModel (pluginlib) | Functional — C172 plugin, capability gating + writeback |
| engine_systems | `src/systems/engine_systems/` | IEnginesModel (pluginlib) | Functional — C172 piston + EC135 turboshaft |
| gear | `src/systems/gear/` | IGearModel (pluginlib) | Functional — C172 fixed-tricycle, WoW, brakes |
| air_data | `src/systems/air_data/` | IAirDataModel (pluginlib) | Functional — pitot-static, icing, turbulence, alt static |
| navigation | `src/systems/navigation/` | None (aircraft-agnostic) | Functional — VOR/ILS/GPS/ADF/DME, failure gating |
| failures | `src/systems/failures/` | None | Functional — catalog, armed triggers, 3-topic routing |
| hydraulic | `src/systems/hydraulic/` | TBD | Stub — heartbeat only |
| ice_protection | `src/systems/ice_protection/` | TBD | Stub — heartbeat only |
| pressurization | `src/systems/pressurization/` | TBD | Stub — heartbeat only |

## The systems node pattern

Every systems node follows this structure:

```cpp
class FooNode : public rclcpp_lifecycle::LifecycleNode {
  // Constructor: use_sim_time=true, declare params, 100ms auto-start timer

  // on_configure: create pubs/subs, load plugin via pluginlib, parse YAML
  //   Wrap in try/catch. On error: RCLCPP_ERROR + SimAlert + return FAILURE
  //   Subscribe to capabilities (transient_local QoS)

  // on_activate: start heartbeat (1Hz wall timer) + update timer
  //   Gate solver on capabilities: FDM_NATIVE → don't run solver
  //   Respect sim state: RUNNING=step(dt), FROZEN=no time advance, RESETTING=reset()
  //   Always publish (IOS needs live data even when frozen)

  // on_deactivate: cancel timers, clear pending state
  // on_cleanup: reset all publishers/subscribers/pointers
};
```

## Standard subscriptions

- `/sim/state` — controls update behavior per sim state
- `/aircraft/fdm/state` — aircraft state
- `/sim/failures/route/<handler>` — failure injection (per handler)
- `/aircraft/controls/panel` — switch commands (nodes with switches)
- `/aircraft/fdm/capabilities` (transient_local) — gate solver

## Standard publishers

- `/sim/diagnostics/heartbeat` (String, 1 Hz) — `rclcpp::Publisher` NOT LifecyclePublisher
- `/sim/diagnostics/lifecycle` (String) — `rclcpp::Publisher` NOT LifecyclePublisher
- `/sim/<system>/state` — system state message
- `/sim/alerts` — config errors, critical failures

## Capability gating

```cpp
bool mode_is_ours = !latest_caps_ ||
  latest_caps_->electrical != sim_msgs::msg::FlightModelCapabilities::FDM_NATIVE;
if (!mode_is_ours) return;
```

## Sim state behavior

| State | Solver | Time advance | Publishing |
|---|---|---|---|
| RUNNING | Full step(dt) | Yes | Yes |
| FROZEN | No step, or step(0) on dirty | No | Yes |
| RESETTING | model->reset() | No | Yes |

## Plugin registration

Aircraft plugins in `src/aircraft/<type>/`:
- `plugins.xml` — pluginlib registration
- `src/<model>.cpp` — implementation
- `config/<system>.yaml` — per-system config

Plugin naming: `aircraft_<id>::<Model>` (e.g., `aircraft_c172::C172ElectricalModel`)

## Build

Single package: `colcon build --packages-select <pkg> && source install/setup.bash`

Package + plugins: `colcon build --packages-select sim_electrical aircraft_c172 aircraft_ec135`