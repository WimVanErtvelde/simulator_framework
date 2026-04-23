---
name: sim-framework
description: >
  Architectural rules, code patterns, and design principles for the FTD/FNPT flight simulator framework.
  This is a ROS2 Jazzy workspace (~22 packages) at ~/simulator_framework on WSL Ubuntu,
  targeting C172 (JSBSim) and EC135 (Helisim) aircraft.

  Use this skill for ANY work in the simulator_framework workspace — creating new ROS2 packages or nodes,
  modifying existing ones, adding message types, wiring failures, writing adapters, designing JSON configs,
  or reviewing architecture decisions. Also use when the user mentions: failures, electrical system,
  navigation/navaids, FDM adapter, IOS, instructor station, sim_manager, audio system, circuit breakers,
  topology JSON, Helisim, JSBSim, or any ROS2 simulator component. If the user asks to create a
  "new node", "new package", or "new subsystem" in this workspace, always consult this skill first.
  When in doubt about whether this skill applies, it probably does — the user works on this project daily.
---

# Flight Simulator Framework — Architecture & Patterns

This skill encodes the architectural invariants, code patterns, and design principles for a
professional FTD Level 2 / FNPT II simulator framework built entirely on ROS2. Following
these rules prevents the most common mistakes that occur during code generation.

## Workspace Layout

```
~/simulator_framework/                    # Colcon workspace root
├── CLAUDE.md                             # Working memory — read first every session
├── DECISIONS.md                          # Decision log (CURRENT STATE + append-only CHANGE LOG)
├── bugs.md                               # Open bug tracker
├── src/                                  # ALL ROS2 packages
│   ├── core/
│   │   ├── sim_manager/                  # Clock, state machine, lifecycle mgmt
│   │   ├── flight_model_adapter/         # IFlightModelAdapter interface + JSBSim/XPlane/Helisim
│   │   ├── input_arbitrator/             # 4-channel source selection (INSTRUCTOR>HARDWARE>VIRTUAL>FROZEN)
│   │   ├── atmosphere_node/              # ISA + weather → /world/atmosphere
│   │   ├── navaid_sim/                   # Navaids, airport/runway DB, terrain LOS
│   │   ├── cigi_bridge/                  # CIGI 3.3 host (raw BE encoding, no CCL)
│   │   └── sim_interfaces/               # Shared C++ headers (no node)
│   ├── systems/
│   │   ├── electrical/                   # Pluginlib → ElectricalSolver, JSON topology
│   │   ├── fuel/                         # Pluginlib → IFuelModel
│   │   ├── engine_systems/               # Pluginlib → IEnginesModel
│   │   ├── gear/                         # Pluginlib → IGearModel
│   │   ├── air_data/                     # Pluginlib → IAirDataModel (pitot-static)
│   │   ├── navigation/                   # No pluginlib — aircraft-agnostic receivers
│   │   ├── failures/                     # Central failure authority — FailureState pipeline
│   │   ├── hydraulic/                    # Stub
│   │   ├── ice_protection/               # Stub
│   │   └── pressurization/               # Stub
│   ├── hardware/
│   │   └── microros_bridge/              # Stub — serial/CAN → /aircraft/devices/hardware/
│   ├── ios_backend/                      # FastAPI + rclpy IOS bridge (ament_python)
│   ├── aircraft/
│   │   ├── c172/                         # Config YAML, plugins, flight model data
│   │   └── ec135/                        # Rotary-wing config and plugins
│   ├── sim_msgs/                         # ALL custom .msg/.srv definitions
│   └── qtg/
│       └── engine/                       # QTG test runner (ament_python)
├── ios/
│   └── frontend/                         # React + Zustand + Vite (NOT a ROS2 package)
├── launch/
│   └── sim_full.launch.py
└── tools/
```

Rule: if it has a `package.xml`, it lives under `src/`. Everything else lives alongside `src/`.
Environment: WSL Ubuntu, user `wim@ProArtPX13`, ROS2 Jazzy.

## Core Principles

### 1. Aircraft-independence through data-driven design

New aircraft support requires **zero C++ code changes** to framework packages. All aircraft-specific
behavior is expressed through configuration files and pluginlib plugins in `src/aircraft/<type>/`.

Config files per aircraft (`src/aircraft/<type>/config/`):
- `config.yaml` — metadata, required nodes, FDM adapter, limits, default IC, gear_points
- `electrical.yaml` — bus topology, sources, loads, switch IDs
- `fuel.yaml` — tanks, selectors, pumps (cockpit interface only — physics owned by FDM)
- `engine.yaml` — engine type, count, panel control IDs
- `gear.yaml` — gear type, retractable flag, leg names
- `air_data.yaml` — pitot-static systems, heat load names
- `failures.yaml` — failure catalog (ATA chapter grouped)
- `navigation.yaml` — installed avionics (drives dynamic IOS A/C page)

When generating code, never hardcode aircraft-specific values. If you find yourself writing
`if (aircraft == "C172")` in framework code, you've made an architectural mistake. The correct
approach is a config field that the aircraft's YAML provides.

### 2. Failure pipeline integrity is non-negotiable

All failure-related state changes flow through `failures` node (`src/systems/failures/`).
The flow is always:

```
IOS frontend → FailureCommand → failures_node → FailureInjection → downstream handlers
                                      ↓
                              FailureState (broadcast — status tracking only)

Note: FailureInjection routing is the ONLY active failure delivery path.
FailureState carries active_failure_ids for UI display and recording.
Systems nodes do NOT subscribe to FailureState for failure handling —
they receive failures via /sim/failures/route/<handler> topics.
```

Routing topics (all recordable in rosbag2):
- `/sim/failures/route/flight_model` → flight_model_adapter
- `/sim/failures/route/electrical` → sim_electrical
- `/sim/failures/route/air_data` → sim_air_data
- `/sim/failures/route/gear` → sim_gear

Never bypass the pipeline. Never publish failure messages from downstream nodes.

### 3. Standalone library + ROS2 node wrapper

Every subsystem is built as two layers:
1. A **standalone C++ library** with no ROS2 dependency — pure logic, testable with GoogleTest
2. A **thin ROS2 node wrapper** that owns subscriptions/publishers and calls the library

Example structure:
```
src/systems/foo/
├── CMakeLists.txt
├── package.xml
├── include/foo/
│   ├── foo_solver.hpp          # Standalone library — NO rclcpp includes
│   └── foo_node.hpp            # ROS2 lifecycle node
├── src/
│   ├── foo_solver.cpp          # Library impl
│   └── foo_node.cpp            # Node wrapper
└── config/
    └── default.yaml
```

### 4. Systems nodes do not cross-subscribe

Systems nodes subscribe to `/aircraft/fdm/state` and publish their own `/aircraft/<system>/state`.
All other coupling goes through `/aircraft/fdm/state`, `/sim/failures/route/<handler>`, or `/world/`.
Cross-subscriptions allowed only for documented physical coupling:
- engines → `/aircraft/electrical/state` (starter bus voltage)
- engines → `/aircraft/fuel/state` (fuel available)
- air_data → `/aircraft/electrical/state` (pitot heat powered)

### 5. Topic namespace convention

Three roots:
- `/world/` — environment and infrastructure that exists independently of the aircraft.
  Weather, navaids, terrain, traffic.
- `/aircraft/` — the simulated machine. FDM, systems, equipment, controls, input devices.
  `/aircraft/devices/` carries raw inputs (hardware, virtual panels, instructor). Only
  input_arbitrator reads `/aircraft/devices/*`, except `/aircraft/devices/instructor/failure_command`
  (read by sim_failures).
- `/sim/` — simulation infrastructure. State machine, diagnostics, CIGI, failure injection,
  scenario engine. Things that wouldn't exist outside a simulator.

Topic categories:
- State (saveable):  `/aircraft/<system>/state`, `/aircraft/controls/*`, `/world/*`
- Commands (transient): `/sim/command`, `/sim/failures/route/*`, `/aircraft/engines/commands`
- Infrastructure (transient): `/sim/diagnostics/*`, `/sim/alerts`, `/sim/cigi/*`, `/sim/terrain/*`, `/clock`

IOS backend publishes inputs to `/aircraft/devices/instructor/` and operational commands to `/sim/command`.
IOS backend NEVER publishes to `/aircraft/*/state` or `/sim/*/state` topics.

Acknowledged exceptions:
- `/sim/command` — IOS publishes SimCommand directly to /sim/ (no arbitration for operational commands)
- sim_engine_systems subscribes to `/aircraft/electrical/state` and `/aircraft/fuel/state` (physical coupling)
- sim_air_data subscribes to `/aircraft/electrical/state` (pitot heat powered)

### 6. Plugin authority model

Each systems node has an abstract interface (`IElectricalModel`, `IFuelModel`, `IEnginesModel`,
`IGearModel`, `IAirDataModel`). Aircraft packages register concrete plugins via `plugins.xml`.
Plugin name convention: `aircraft_<id>::<X>Model` (e.g., `aircraft_c172::C172ElectricalModel`).
The plugin has full authority over its published state.

### 7. CapabilityMode tri-state

`FlightModelCapabilities` uses `CapabilityMode` enum per subsystem:
- `FDM_NATIVE` — FDM handles it internally, our node is display-only
- `EXTERNAL_COUPLED` — our node runs solver + writes back to FDM each cycle
- `EXTERNAL_DECOUPLED` — our node runs independently, no FDM write-back

JSBSim: electrical=EXTERNAL_COUPLED, fuel=EXTERNAL_COUPLED, air_data=EXTERNAL_DECOUPLED.
Published as transient_local on `/aircraft/fdm/capabilities` at startup.

## Common Pitfalls

1. **Publishing failure messages outside the pipeline** — breaks state tracking and recording
2. **Hardcoding aircraft values** — use config. Ask: "would I change this C++ for a new aircraft?"
3. **Putting rclcpp in solver logic** — solver library must work without ROS2
4. **Stale build after .msg changes** — full `colcon build` + restart all nodes
5. **Subscribing to `/aircraft/devices/` from a sim node** — only input_arbitrator reads device topics
6. **Not checking error returns** — lifecycle transitions, file loads, service calls can fail
7. **Over-engineering for DIS/HLA/SISO** — ROS2 topics are the backbone, no external standards

## Message Conventions

All messages in `src/sim_msgs/`:
- State messages (periodic): end in `State` (e.g., `EngineState`, `FuelState`)
- Command messages: end in `Command` (e.g., `SimCommand`, `FailureCommand`)
- Frequency fields: `_freq_mhz` or `_freq_khz` — never bare `_freq`
- NavigationState: `float64` for lat/lon only, `float32` for all other numerics
- Avionics numbering: GPS1/GPS2, ADF1/ADF2 — consistent `1`/`2` suffix

CDR deserialization mismatches after message changes are the #1 cause of mysterious runtime
failures. Fix: full `colcon build` + restart all nodes.

## Helisim Integration Notes

Helisim is used as the FDM **only**. Real-time data via UDP: 6-word header + 268-word data
section. The adapter reads this stream and publishes to `/aircraft/fdm/state`.
See `references/helisim-icd-quickref.md` for commonly needed word offsets.