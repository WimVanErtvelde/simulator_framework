# CLAUDE.md — Simulator Framework

This file is the working memory for Claude Code sessions.
Read the WORKFLOW section first. Read REFERENCE sections as needed for the task at hand.

---

## Workflow Rules — READ THIS FIRST

### Before ANY code change:

1. **Read the relevant source files** — do not assume you know what's there
2. **State back your plan** in this format:
   - MODIFYING: [file list]
   - CURRENT BEHAVIOR: [what the code does now]
   - NEW BEHAVIOR: [what it will do after your change]
   - COULD BREAK: [what might go wrong]
3. **WAIT for approval** — do not proceed until Wim confirms
4. After approval, make the change, then `colcon build --packages-select <pkg>` to verify

### Task card format

When Wim gives you a task, it will follow this structure. Do NOT expand scope beyond it:

```
TASK: [short name]
GOAL: [one sentence — what changes when this is done]
FILES TO CREATE: [explicit list, or "none"]
FILES TO MODIFY: [explicit list]
DO NOT TOUCH: [files/packages that are off-limits]
READS FROM: [configs, topics, messages this code consumes]
PUBLISHES TO: [topics this code produces]
ACCEPTANCE: [build command, observable behavior, or test]
CONSTRAINTS: [no new deps, no message changes, etc.]
```

If a task card is provided, follow it literally. If something seems wrong or missing,
ask — do not improvise. If no task card is provided, create one yourself and present it
for approval before writing any code.

### Diagnostic-first rule

When investigating a bug: (1) add diagnostic logging, (2) build + observe,
(3) report findings with log evidence, (4) propose a fix with reasoning,
(5) WAIT for approval. Never apply a fix before understanding the root cause.

### Git commit discipline

Commit after every successfully building change. One logical change per commit.

### What to check before finishing

- [ ] `colcon build` passes (at minimum the affected packages)
- [ ] No hardcoded aircraft-specific values in framework code
- [ ] No new cross-node subscriptions that violate topic conventions
- [ ] Error paths handled (not just happy path)
- [ ] If you modified a .msg or .srv, full rebuild + all consumers still compile
- [ ] If you modified `ios_backend/` (or any ament_python package), rebuild with
      `--symlink-install` before testing — otherwise the running backend imports
      the stale installed copy and your changes are invisible

### bugs.md

`bugs.md` in repo root tracks known issues. Before modifying a file that has an open bug,
check bugs.md first. After fixing a bug, update bugs.md.

### Doc update task cards

Design sessions produce "doc update" cards with exact text replacements for .md files.
Apply edits mechanically — don't rephrase, don't touch source code. If a replacement
target doesn't match, report the mismatch rather than guess.

---

## Startup Sequence

Three terminals required. Start in order:

**Terminal 1 — Simulator**
```bash
./start_sim.sh
```

**Terminal 2 — IOS Backend** (wait until sim prints "All N required nodes alive")
```bash
./start_backend.sh
```
Backend takes ~5 seconds before ROS2 data flows (3s DDS discovery + 2s initial subscription fill).

**Terminal 3 — IOS Frontend**
```bash
./start_frontend.sh
# → http://localhost:5173
```

**Terminal 4 — PlotJuggler** (optional, for FDM tuning / QTG)
```bash
./start_plotjuggler.sh
```
Connect via Streaming → ROS2 Topic Subscriber. Select topics to plot.
Useful topics for FDM tuning:
- `/aircraft/fdm/state` — all truth values (position, attitude, velocities)
- `/aircraft/air_data/state` — instrument readings (IAS, altitude, VSI)
- `/aircraft/electrical/state` — bus voltages, load states
- `/aircraft/fuel/state` — tank quantities, flow rates
- `/aircraft/engines/state` — RPM, MAP, temps

**Notes**
- Backend MUST be started with ROS2 sourced (DDS isolation otherwise)
- Backend uses `rclpy.spin_once()` in a thread (`rclpy.spin()` silently fails under uvicorn)
- Kill stale: `fuser -k 8080/tcp` (backend), `fuser -k 5173/tcp` (frontend)

---

## Cross-session continuity — DECISIONS.md

`DECISIONS.md` in the repo root is the **architectural decision log** for this project.

**Structure:**
- `## CURRENT STATE` at the top — editable living summary, updated periodically
- `## CHANGE LOG` below — append-only forever, never edit past entries

**Claude Code must:**
- Read `DECISIONS.md` at the start of every session (CURRENT STATE first, then relevant log entries)
- Append an entry whenever a structural decision is made:
  new message type, changed topic name, deferred feature, refactored node interface, etc.
- Never delete or edit past log entries
- Format: `## YYYY-MM-DD — hh:mm:ss - Claude Code` followed by `- DECIDED / REASON / AFFECTS` lines

---

## Project Goal

A **reusable, modular flight simulator framework** targeting FNPT II and FTD Level 2 qualification.
Designed to support both **fixed-wing and rotary-wing** aircraft.

The framework itself does not need to be certified — only the flight model data and its
validation output (QTG) need to meet authority requirements.

---

## Core Design Principles

1. **Swappable FDM** — flight model behind an adapter interface. Sim never calls FDM code directly.
2. **Swappable IG** — visual system decoupled via CIGI.
3. **ROS2 backbone** — all systems communicate via topics, no cross-node function calls.
4. **No cross-subscribes** except documented physical dependencies (engines→electrical,
   engines→fuel, air_data→electrical). Other coupling goes through `/aircraft/fdm/state`,
   `/sim/failures/route/<handler>`, or `/world/`.
5. **Input arbitration** — all control inputs go through Input Arbitrator. No sim node
   reads device topics directly. Per-channel source selection configurable at runtime via IOS.
6. **Aircraft config drives everything** — nodes, FDM, panels all driven by YAML per aircraft.
7. **IOS is purely a control surface** — injects commands via ROS2, no privileged access.
8. **World is shared infrastructure** — atmosphere/weather/nav/terrain live under `/world/`.
   Systems nodes never compute environmental state.

---

## Tech Stack

| Layer | Technology |
|---|---|
| Middleware | ROS2 Jazzy (LTS) |
| Systems nodes | C++ (real-time, deterministic) |
| Flight Model Adapter | C++ with abstract plugin interface |
| IOS backend | Python / FastAPI + rclpy |
| IOS frontend | React + Zustand + WebSocket + React Router |
| Virtual cockpit pages | Web (React), URL-routed per panel |
| Hardware MCUs | ESP32 or STM32 with micro-ROS |
| micro-ROS agent | ROS2 node bridging serial/CAN to ROS2 topics |
| Aircraft config | YAML |
| Replay | ROS2 bag (native) |
| QTG reports | Python + matplotlib + ReportLab |
| CIGI | CIGI 3.3 / 4.0 (host-side implementation in C++) |

---

## Repository Structure

All ROS2 packages live under `src/`. The workspace root contains only non-package
directories (frontend, launch files, tools) and colcon output (build/, install/, log/).
`colcon build` is always run from `~/simulator_framework/`.

```
simulator_framework/
├── CLAUDE.md                        ← this file
├── DECISIONS.md                     ← decision log (CURRENT STATE + append-only CHANGE LOG)
├── src/                             ← ALL ROS2 packages
│   ├── core/
│   │   ├── sim_manager/             ← sim clock, state machine, lifecycle mgmt
│   │   ├── flight_model_adapter/    ← IFlightModelAdapter interface + implementations
│   │   ├── input_arbitrator/        ← per-channel source selection
│   │   ├── navaid_sim/              ← VOR/ILS/NDB/DME/markers, DTED LOS, A424+XP format
│   │   ├── cigi_bridge/             ← CIGI host implementation
│   │   └── sim_interfaces/          ← headers-only shared C++ interfaces
│   ├── systems/
│   │   ├── electrical/              ← DC/AC buses, breakers (pluginlib, GraphSolver)
│   │   ├── fuel/                    ← tanks, feed, transfer, CG
│   │   ├── engine_systems/          ← N1/N2, EGT, torque, start sequence
│   │   ├── gear/                    ← WoW, position, brakes, nosewheel (pluginlib)
│   │   ├── air_data/                ← pitot-static instrument model (pluginlib)
│   │   ├── navigation/              ← onboard receivers VOR/ILS/GPS/ADF (no pluginlib)
│   │   ├── failures/                ← failure injector, active failure broadcast
│   │   ├── hydraulic/               ← stub (not launched for C172)
│   │   ├── ice_protection/          ← stub (not launched for C172)
│   │   └── pressurization/          ← stub (not launched for C172)
│   ├── hardware/microros_bridge/    ← serial/CAN → /aircraft/devices/hardware/
│   ├── ios_backend/                 ← ament_python: FastAPI + rclpy IOS bridge
│   ├── aircraft/{c172,ec135}/       ← config YAML + plugins per aircraft
│   ├── sim_msgs/                    ← all custom message definitions
│   ├── world/weather_solver/        ← wind, Dryden turbulence, microburst → /world/atmosphere
│   └── qtg/engine/                  ← ament_python: test runner + QTG reports
├── ios/frontend/                    ← React web app (NOT a ROS2 package)
├── launch/sim_full.launch.py        ← launches all sim nodes (NOT ios_backend)
└── tools/scenario_editor/           ← optional GUI scenario builder
```

**Rule:** if it has a `package.xml`, it lives under `src/`. Everything else lives alongside `src/`.

---

## Sim Manager

**The heartbeat of the framework.**

- Uses ROS2 native sim time (`/clock` topic + `use_sim_time: true` on all nodes)
- All ROS2 tooling (rosbag, rviz, rqt) understands sim time natively — no custom clock needed
- Sim Manager owns and drives the `/clock` topic at fixed rate (50 Hz default, configurable)
- Uses wall timer for `/clock` (cannot use sim time to drive itself)
- Manages sim state machine:

```
INIT → READY → RUNNING ↔ FROZEN → RESETTING → READY
         ↓                            ↓
       FROZEN (reposition)        SHUTDOWN
```

- Loads aircraft config YAML on startup (search path: parameter → `src/aircraft/<id>/config/config.yaml` → installed share)
- Spawns/tears down system nodes based on aircraft config
- Exposes a ROS2 service interface for IOS commands
- Heartbeat monitoring: 2-second timeout, auto-freeze on required node loss
- RESETTING state: 100ms wall timer before transitioning to READY (gives nodes time to receive IC broadcast)
- Repositioning: CMD_REPOSITION → FROZEN + `reposition_active` flag → broadcast IC → wait for
  `/sim/terrain/ready` → return to previous state. No separate REPOSITIONING state.
  Rejects INIT, SHUTDOWN, RESETTING states.
  Timeouts: sim_manager 15s (overall), FMA pending_ic_ 30s (terrain loading).

---

## Flight Model Adapter

Abstract C++ interface — never instantiate a concrete flight model outside this adapter.

```cpp
class IFlightModelAdapter {
public:
    virtual void initialize(const std::string& aircraft_id) = 0;
    virtual void apply_initial_conditions(const InitialConditions& ic) = 0;
    virtual FlightModelState step(double dt) = 0;
    virtual FlightModelState get_state() = 0;
};
```

**Implementations (under `src/core/flight_model_adapter/`):** `JSBSimAdapter`
(FetchContent v1.2.1, currently the only working implementation). UDP adapters for
X-Plane / Helisim and a custom certified adapter are planned.

**JSBSim notes:**
- Uses wall timer (not sim timer) for its 50Hz update loop — drives JSBSim independently
- Default IC: EBBR rwy 25L, on ground, engine running at idle (ready_for_takeoff)
- Engine start uses `propulsion/set-running` (not per-engine — initializes magnetos correctly)
- FlightModelState carries position in ECG (lat/lon/alt) AND ECEF (x/y/z); velocity in NED, Body, and ECEF frames

**Lat/lon units:** All ROS2 messages carry lat/lon in **degrees**. JSBSimAdapter is the
single conversion point (JSBSim radians → degrees). Internal solver code may use radians
for trig — convert deg→rad locally at point of use. Attitude angles (roll, pitch, heading)
remain in radians.

**Terrain refinement on reposition:**
- IC arrives → apply to JSBSim (runway DB altitude) → set `pending_ic_` → wait for CIGI HOT
- HOT arrives (from cigi_bridge, gated by IG Status Operate) → `refine_terrain_altitude()` adjusts altitude + terrain
- `refine_terrain_altitude` uses `RunIC()` with cockpit state save/restore (avoids SetAltitudeASL cache issue)
- `pending_ic_` gates FDM stepping — JSBSim does not `step()` while waiting for terrain
- 30s timeout: if no CIGI HOT arrives, clear `pending_ic_` and accept runway DB altitude

**Publishes:** `/aircraft/fdm/state` (FlightModelState — position, attitude, velocities, accelerations, aero forces, WoW)

---

## ROS2 Topic Conventions

### Naming rule

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

SimSnapshot rule: save topics matching `*/state` or `*_state`, plus `/aircraft/controls/*` and `/world/*`.

Acknowledged exceptions:
- `/sim/command` — IOS publishes SimCommand directly to /sim/ (no arbitration for operational commands)
- sim_engine_systems subscribes to /aircraft/electrical/state (bus voltage for starter) and /aircraft/fuel/state (fuel available) — physical coupling
- sim_air_data subscribes to /aircraft/electrical/state (pitot heat powered) — physical coupling

All topics use `snake_case`. No abbreviations unless universally understood (e.g. `flight_model`, `cigi`).

### Device topics (raw inputs — never read by sim nodes directly)

| Topic | Type | Publisher | Notes |
|---|---|---|---|
| `/aircraft/devices/hardware/controls/flight` | RawFlightControls | microros_bridge | Yoke, pedals, collective from MCU |
| `/aircraft/devices/hardware/controls/engine` | RawEngineControls | microros_bridge | Throttle, mixture, condition from MCU |
| `/aircraft/devices/hardware/controls/avionics` | RawAvionicsControls | microros_bridge | Radio, nav, transponder from MCU |
| `/aircraft/devices/hardware/panel` | PanelControls | microros_bridge | Physical switches, CBs, selectors from MCU |
| `/aircraft/devices/hardware/heartbeat` | DeviceHeartbeat | microros_bridge | Per-channel health, used by arbitrator |
| `/aircraft/devices/virtual/controls/flight` | RawFlightControls | virtual cockpit pages | Same type as hardware equivalent |
| `/aircraft/devices/virtual/controls/engine` | RawEngineControls | virtual cockpit pages | Same type as hardware equivalent |
| `/aircraft/devices/virtual/controls/avionics` | RawAvionicsControls | virtual cockpit pages | Future virtual avionics head |
| `/aircraft/devices/virtual/panel` | PanelControls | virtual cockpit pages | Virtual switch panels (VIRTUAL priority) |
| `/aircraft/devices/instructor/controls/flight` | RawFlightControls | ios_backend | Instructor takeover input |
| `/aircraft/devices/instructor/controls/engine` | RawEngineControls | ios_backend | Instructor engine override |
| `/aircraft/devices/instructor/controls/avionics` | RawAvionicsControls | ios_backend | IOS frequency tuning (INSTRUCTOR priority) |
| `/aircraft/devices/instructor/panel` | PanelControls | ios_backend | IOS switch overrides (INSTRUCTOR priority) |

### Arbitrated sim topics (what the sim actually reads)

| Topic | Type | Publisher | Notes |
|---|---|---|---|
| `/clock` | rosgraph_msgs/Clock | sim_manager | ROS2 native sim time — drives all nodes |
| `/sim/state` | SimState | sim_manager | INIT, READY, RUNNING, FROZEN, RESETTING |
| `/aircraft/fdm/state` | FlightModelState | flight_model_adapter | Position, attitude, velocities, forces |
| `/aircraft/controls/flight` | FlightControls | input_arbitrator | Authoritative flight controls output |
| `/aircraft/controls/engine` | EngineControls | input_arbitrator | Authoritative engine controls output |
| `/aircraft/controls/avionics` | AvionicsControls | input_arbitrator | Authoritative avionics controls output |
| `/aircraft/controls/panel` | PanelControls | input_arbitrator | Authoritative panel switch/CB output |
| `/aircraft/controls/arbitration` | ArbitrationState | input_arbitrator | Per-channel source (HARDWARE/VIRTUAL/INSTRUCTOR/FROZEN) |
| `/aircraft/electrical/state` | ElectricalState | sim_electrical | DC/AC buses, sources, loads, SOC |
| `/aircraft/fuel/state` | FuelState | sim_fuel | Quantities, flow, CG |
| `/aircraft/engines/state` | EngineState | sim_engine_systems | N1/N2, EGT, torque |
| `/aircraft/engines/commands` | EngineCommands | sim_engine_systems | Includes starter_engage[4]; turboprop/FADEC writeback |
| `/aircraft/gear/state` | GearState | sim_gear | WoW per leg, position, brakes, nosewheel |
| `/aircraft/air_data/state` | AirDataState | sim_air_data | Instrument IAS, altitude, VSI (pitot-static) |
| `/aircraft/navigation/state` | NavigationState | sim_navigation | Onboard receiver outputs: VOR, ILS, GPS, ADF, DME, TACAN |
| `/sim/failures/state` | FailureState | sim_failures | Active failure IDs (status tracking + IOS) |
| `/sim/failures/route/flight_model` | FailureInjection | sim_failures | Routed to flight_model_adapter |
| `/sim/failures/route/electrical` | FailureInjection | sim_failures | Routed to sim_electrical |
| `/sim/failures/route/air_data` | FailureInjection | sim_failures | Routed to sim_air_data |
| `/sim/failures/route/gear` | FailureInjection | sim_failures | Routed to sim_gear |
| `/sim/alerts` | SimAlert | any node | SEVERITY_INFO/WARN/CRITICAL alerts to IOS |
| `/sim/terrain/ready` | std_msgs/Bool | flight_model_adapter | Signals terrain loaded after reposition |
| `/sim/terrain/source` | TerrainSource | flight_model_adapter | CIGI/SRTM/MSL indicator |
| `/aircraft/writeback/electrical` | ElectricalState | sim_electrical | Coupled writeback to FDM |
| `/aircraft/writeback/fuel` | FuelState | sim_fuel | Coupled writeback to FDM |
| `/sim/cigi/hat_responses` | HatHotResponse | cigi_bridge | HOT terrain elevation per gear point; also surface_type, static_friction_factor, rolling_friction_factor (custom extension) |
| `/sim/cigi/ig_status` | std_msgs/UInt8 | cigi_bridge | SOF IG Status (0=Standby, 2=Operate) |
| `/sim/cigi/host_to_ig` | CigiPacket | cigi_bridge | Host → IG packets (planned — recording/debug) |
| `/sim/cigi/ig_to_host` | CigiPacket | cigi_bridge | IG → Host packets (planned — recording/debug) |
| `/aircraft/devices/instructor/failure_command` | FailureCommand | ios_backend | IOS failure inject/clear → sim_failures |

### Diagnostics topics

| Topic | Type | Publisher | Notes |
|---|---|---|---|
| `/sim/diagnostics/heartbeat` | std_msgs/String | each node | Node name as data, published at 1 Hz |
| `/sim/diagnostics/lifecycle` | std_msgs/String | each node | Format: "node_name:state" on every transition |

### World environment topics

| Topic | Type | Publisher | Notes |
|---|---|---|---|
| `/world/atmosphere` | AtmosphereState | weather_solver | ISA + weather deviation + interpolated wind + Dryden turbulence + microburst at aircraft position |
| `/world/weather` | WeatherState | sim_manager | Cloud layers, wind layers, precipitation, surface, turbulence model, microbursts |
| `/world/hazards/microburst` | MicroburstHazard | weather_solver | Active microburst fields (Oseguera-Bowles parameters) |
| `/world/nav_signals` | NavSignalTable | navaid_sim | Receivable navaids, signal strength, LOS |
| `/world/terrain` | TerrainState | terrain_node (TBD) | DTED elevation + obstruction data |

---

## Input Arbitrator

**Single source of truth for all control inputs.**

Source priority per channel (highest to lowest):
```
INSTRUCTOR  → explicit IOS command (/aircraft/devices/instructor/)
HARDWARE    → real MCU input, when healthy (/aircraft/devices/hardware/)
VIRTUAL     → virtual panel fallback (/aircraft/devices/virtual/)
FROZEN      → hold last value
```

Channels: `flight`, `engine`, `avionics` — 3 continuous channels with sticky instructor takeover.

Hardware timeout > 500ms → auto-fallback to VIRTUAL + alert on `/aircraft/controls/arbitration`.

Instructor takeover is **sticky** for flight/engine/avionics — once instructor publishes on a
channel, source stays INSTRUCTOR until node reconfigure. No auto-release, no timeout.

**Panel channel — per-switch FORCE model (NOT sticky):** each switch tracks its own
force state independently via `switch_forced[]` in PanelControls. Forcing `sw_battery`
doesn't lock out cockpit's `sw_landing_lt`. Effective value: forced > hardware (if
healthy) > virtual. `ArbitrationState.forced_switch_ids[]` reports which switches are
currently forced. Flight/engine/avionics channels remain sticky (safety).

---

## IOS Command Architecture

IOS A/C page publishes to `/aircraft/devices/instructor/*` (INSTRUCTOR). Virtual
cockpit pages publish to `/aircraft/devices/virtual/*` (VIRTUAL). Physical hardware
publishes to `/aircraft/devices/hardware/*` (HARDWARE).

IOS A/C page switches have a FORCE checkbox — ticking it locks the switch (cockpit/
hardware cannot override). Toggling without the checkbox also implicitly forces.
`PanelControls.msg` carries `switch_forced[]` / `selector_forced[]` arrays: empty =
normal command, populated = force/release. All panel UIs read displayed state from
`/aircraft/controls/panel` (arbitrated output) — never from their own published commands.

---

## IOS Backend (`src/ios_backend/`)

FastAPI + rclpy. Bridges ROS2 ↔ WebSocket ↔ React frontend.
Run manually (not via launch file) so output is visible and it can be restarted independently.

**⚠ Build constraint — ament_python packages rebuild on every edit:**
`start_backend.sh` sources `install/setup.bash`, so Python imports from the installed
site-packages copy, not the source tree. After any `src/ios_backend/` (or `qtg_engine`)
edit, run `colcon build --packages-select ios_backend --symlink-install` and restart.
Frontend changes go through Vite HMR — no rebuild needed.

**Node discovery:** fully dynamic — heartbeats + lifecycle_state messages + ROS2 graph queries
every 3 seconds. **No hardcoded node list.** Has a 5-second startup delay before first query.

**Lifecycle state inference:** receiving a heartbeat implies `active` state for nodes whose
lifecycle transitions were missed before ios_backend subscribed.

**Node status logic:**
- `OK` — heartbeat age < 2s OR node in graph
- `DEGRADED` — heartbeat age 2–5s
- `LOST` — heartbeat age 5–10s but node still in graph
- `OFFLINE` — not in ROS2 graph and no recent heartbeat

**SimCommand constants** (src/sim_msgs/msg/SimCommand.msg):
```
CMD_RUN             = 0
CMD_FREEZE          = 1
CMD_UNFREEZE        = 2
CMD_RESET           = 3
CMD_LOAD_SCENARIO   = 4
CMD_SHUTDOWN        = 5
CMD_SET_IC          = 6   # update stored IC only — no reposition triggered
CMD_RELOAD_NODE     = 7   # payload: {node_name}
CMD_DEACTIVATE_NODE = 8   # payload: {node_name}
CMD_ACTIVATE_NODE   = 9   # payload: {node_name}
CMD_RESET_NODE      = 10  # payload: {node_name} — chains deactivate→cleanup→configure→activate
CMD_REPOSITION      = 11  # payload: IC fields — triggers FROZEN + terrain wait + return to prev state
```

**Topic subscriptions:** `/aircraft/fdm/state`, `/sim/state`, `/aircraft/fuel/state`, `/aircraft/navigation/state`,
`/aircraft/controls/avionics`, `/aircraft/electrical/state`, `/sim/alerts`, `/aircraft/engines/state`,
`/aircraft/air_data/state`, `/aircraft/gear/state`, `/sim/failures/state`, `/aircraft/controls/arbitration`,
`/sim/terrain/source`, `/sim/diagnostics/heartbeat`, `/sim/diagnostics/lifecycle`

**Topic publishers:** `/aircraft/devices/instructor/panel`, `/aircraft/devices/instructor/controls/avionics`,
`/aircraft/devices/virtual/panel` (for cockpit pages via separate WS handler),
`/aircraft/devices/instructor/failure_command` (failure inject/clear → sim_failures)

---

## IOS Frontend (`ios/frontend/`)

React + Zustand + WebSocket + React Router. Served by Vite dev server on port 5173.
**Not a ROS2 package** — lives outside `src/`.

**URL routing:**
- `/` — main IOS app (map, status strip, 9 panel tabs, action bar)
- `/cockpit/c172/electrical` — virtual C172 electrical panel (VIRTUAL priority switches)
- `/cockpit/c172/avionics` — virtual C172 avionics panel (placeholder)

**Key files:**
- `src/main.jsx` — React Router entry
- `src/store/useSimStore.js` — Zustand store, WebSocket handler, CMD dispatch
- `src/components/StatusStrip.jsx` — top 3-row status (Row 3 dynamic from navigation.yaml)
- `src/components/MapView.jsx` — Leaflet map, type-aware aircraft icon
- `src/components/panels/NodesPanel.jsx` — dynamic node discovery, lifecycle, controls
- `src/components/panels/AircraftPanel.jsx` — fully dynamic A/C page from YAML configs
- `src/components/cockpit/CockpitElectrical.jsx` — virtual cockpit electrical panel (VIRTUAL)

**Config-driven panels:** ios_backend loads aircraft YAMLs and sends them as WS messages
on connect. Frontend stores and renders dynamically: `avionics_config`, `engine_config`,
`fuel_config`, `failures_config`, `electrical_config`.

IOS switches render amber (instructor authority). Virtual cockpit switches render green.

---

## Systems Nodes

Each system node follows this pattern:
- Uses ROS2 sim time (`use_sim_time: true`) — timer driven by `/clock`
- Implemented as `rclcpp_lifecycle::LifecycleNode`
- Auto-activates on startup via `trigger_transition()` in a 100ms timer
- Subscribes to `/aircraft/fdm/state`, relevant `/aircraft/controls/` and `/world/` topics, and `/sim/failures/route/<handler>` for failure injection
- Publishes its `/aircraft/<system>/state` topic
- Publishes heartbeat to `/sim/diagnostics/heartbeat` at 1 Hz (node name as String data)
- Publishes lifecycle transitions to `/sim/diagnostics/lifecycle` ("name:state" format)
- Heartbeat and lifecycle publishers are `rclcpp::Publisher` (NOT LifecyclePublisher) — must publish in all states
- Respects sim state from `/sim/state`:
  - RUNNING → full update at node rate
  - FROZEN → no SOC/time advance, keep publishing (IOS needs live data)
  - RESETTING → reload initial conditions from YAML via `model->reset()`

**YAML error handling:**
- `on_configure()` wraps plugin load + YAML parse in try/catch
- On error: `RCLCPP_ERROR` + publish `SimAlert` to `/sim/alerts` + return `FAILURE`
- Node stays UNCONFIGURED, operator fixes YAML, hits RELOAD to recover
- RELOAD logs: "Reloading `<system>` config from: `<path>`"

**Nodes using pluginlib** (plugin name = `aircraft_<id>::<X>Model`):
`sim_electrical`, `sim_fuel`, `sim_gear`, `sim_air_data`, `sim_engine_systems`

**Nodes NOT using pluginlib** (aircraft-agnostic):
`sim_navigation` — receiver behavior is standard across aircraft types

---

## sim_air_data (`src/systems/air_data/`)

Pitot-static instrument model. Computes instrument IAS, altitude, VSI from truth + system state.

- Subscribes to `/aircraft/fdm/state` (TAS, altitude truth), `/world/atmosphere` (pressure, temperature, QNH), `/world/weather` (turbulence, visible_moisture), `/aircraft/electrical/state` (pitot heat load powered), `/aircraft/controls/panel` (alternate static valve), `/sim/failures/route/air_data`
- Publishes `/aircraft/air_data/state` (AirDataState) at 50Hz
- Supports up to 3 pitot-static systems (C172=1, glass cockpit=3)
- IOS and cockpit displays read AirDataState for IAS/ALT/VSI (not FlightModelState)
- FlightModelState remains truth (for QTG, recording, CIGI, IOS TRUTH display)

**Pitot-static physics:** pitot blocked (drain clear) → IAS decays; drain blocked → IAS
acts like altimeter. Static port blocked → altitude freezes, VSI zero. Alternate static →
cabin pressure offset (~-30Pa for C172).

**Icing:** visible_moisture + OAT < 5°C + pitot heat off → ice accumulates (45s default),
clears at 2x rate with heat on. Pitot heat resolved from ElectricalState load_powered
(CB popped = no heat). Turbulence adds band-limited noise to IAS.

---

## sim_gear (`src/systems/gear/`)

Landing gear system. Reads FDM gear contact data, publishes aggregated gear state.

- Subscribes to `/aircraft/fdm/state` (WoW, gear position, steering), `/aircraft/controls/flight` (gear handle, brakes), `/sim/failures/route/gear`
- Publishes `/aircraft/gear/state` (GearState) at 50Hz
- C172 plugin: fixed tricycle, position always 1.0, WoW from FDM, nosewheel angle, brake echo
- Supports gear_unsafe_indication failure for warning light test
- GearState: ios_backend subscriber added per aircraft type. C172 fixed gear = no IOS consumer.

---

## Failure Routing

Failures are cataloged in `failures.yaml` per aircraft. `sim_failures` routes injections to
handler-specific topics under `/sim/failures/route/`: `flight_model`, `electrical`, `air_data`,
`gear`. Handler `sim_failures` handles nav receiver/instrument failures internally.

**Ground station failures** (VOR/ILS/NDB off air) are world conditions, not aircraft equipment
failures. Will be implemented as IOS→navaid_sim direct command via `/world/navaid_command`.
NOT routed through sim_failures.

**ScenarioEvent** — placeholder message for future scenario/CGF engine. Not yet implemented.

---

## navaid_sim (`src/core/navaid_sim/`)

Core framework package. Ground navaid environment node.

- Subscribes to `/aircraft/fdm/state` + `/aircraft/controls/avionics`
- Publishes `/world/nav_signals` (NavSignalTable) at 10 Hz
- Data source via YAML param `data_source`: `"euramec"` (EURAMEC.PC) or `"xplane"`
  (earth_nav.dat, XP810/XP12 auto-detected) + WMM.COF
- SRTM terrain LOS checks on all VHF receivers
- Airport DB supports apt.dat (xp12) and ARINC-424 (euramec.pc) — provides runway
  threshold lat/lon, displaced threshold, elevation
- IOS position panel offsets displaced_threshold + 30m along runway heading

---

## navigation_node (`src/systems/navigation/`)

Onboard receiver layer — aircraft-agnostic, no pluginlib.

- Subscribes to `/aircraft/fdm/state`, `/world/nav_signals`, `/aircraft/controls/avionics`
- Publishes `/aircraft/navigation/state` (NavigationState.msg) at 10 Hz
- VOR CDI: `(OBS - radial) / 4.0` deg/dot, clamped ±2.5 dots
- LOC CDI: deviation_dots from navaid_sim directly, clamped ±2.5
- GS dots: passed through, clamped ±2.5
- DME: NAV1/HOLD/NAV2 selector via `dme_source` in AvionicsControls
- GPS: derived from FlightModelState (always valid, RAIM deferred)
- TACAN, GPS2, ADF2 fields zeroed pending implementation

---

## Avionics Message Tiers

Three-tier pipeline — never skip a tier:

```
RawAvionicsControls   device input, one per source (/aircraft/devices/*/controls/avionics)
       ↓ input_arbitrator
AvionicsControls      arbitrated authoritative state (/aircraft/controls/avionics)
       ↓ navigation_node
NavigationState       computed instrument outputs (/aircraft/navigation/state)
```

**Field naming conventions (locked):**
- MHz values: `_freq_mhz` suffix (e.g. `nav1_freq_mhz`, `com1_freq_mhz`)
- kHz values: `_freq_khz` suffix (e.g. `adf1_freq_khz`)
- No bare `_freq` suffix anywhere
- Numbering: `adf1`/`adf2`, `gps1`/`gps2` — consistent `1`/`2` suffix, never unprefixed

**NavigationState field types:**
- `float64` for lat/lon (degrees) only
- `float32` for all other numeric fields
- `bool` for flags, `string` for idents

**Normalized field conventions (locked):**
- `_norm` = 0.0-1.0 or -1.0-1.0 normalized ratio (gear position, fuel fraction, control deflection, ice accumulation)
- `_pct` = 0.0-100.0 percentage (N1%, N2%, torque%, flap deployment%, battery SOC%)
- If a field stores 0-1 it MUST use `_norm`, never `_pct`
- Dimensionless fields (mach, load_factor, epr) have no unit suffix — this is correct

**NavigationState** includes frequency echoes from AvionicsControls for IOS display
convenience. COM3, ADF2, TACAN, GPS2 fields are zeroed pending implementation.

**Installed avionics per aircraft** defined in `src/aircraft/<type>/config/navigation.yaml`.
IOS A/C page and StatusStrip Row 3 render dynamically from this config.

---

## Aircraft Configuration

Each aircraft: `src/aircraft/<type>/` — contains `package.xml`, `CMakeLists.txt`, `plugins.xml`,
`src/` (plugin implementations), and `config/` (per-system YAML files).

Config files per aircraft (all under `config/`):
- `config.yaml` — metadata, required nodes, FDM adapter, limits, default IC, gear_points
- `electrical.yaml` — graph v2: nodes (sources/buses/junctions/loads) + connections
- `fuel.yaml`, `engine.yaml`, `gear.yaml`, `air_data.yaml` — per-system topology
- `failures.yaml` — failure catalog (ATA-grouped), injection handlers
- `navigation.yaml` — installed avionics (drives dynamic IOS A/C page)

**Panel control ID naming convention:**
- `sw_` — boolean switch (e.g. `sw_battery`, `sw_alt`)
- `btn_` — momentary button
- `cb_` — circuit breaker
- `sel_` — detented rotary selector (e.g. `sel_fuel`)
- `pot_` — analog potentiometer
- `enc_abs_` / `enc_rel_` — absolute / relative encoder

---

## sim_electrical Behaviour Per Sim State

| State | Solver | Battery SOC | Publishing |
|---|---|---|---|
| RUNNING | Full 50Hz `step()` | Drains normally | Yes, 50Hz |
| FROZEN | No `step()` | Does NOT drain | Yes, 50Hz |
| FROZEN + switch change | Runs once with dt=0 (`solver_dirty` flag) | No change | Yes, reflects new state |
| RESETTING | `model->reset()` | Restored from YAML | Yes |

`IElectricalModel::reset()` reloads initial conditions from the already-parsed topology
(does NOT re-read YAML — use RELOAD for that).

---

## CIGI Bridge (`src/core/cigi_bridge/`)

- CIGI 3.3 host side (raw BE encoding, no CCL dependency)
- Subscribes to `/aircraft/fdm/state`, `/sim/state` (for `reposition_active`)
- Publishes `/sim/cigi/hat_responses` (HOT terrain data from IG)
- Sends Entity Control (position/attitude) + IG Control (mode) at 60 Hz via UDP
- IG identity hidden from rest of sim

**Repositioning handshake (CIGI 3.3 compliant):**
- On `reposition_active` rising edge: send IG Mode = Reset for ONE frame, clear HOT tracker
- Subsequent frames: IG Mode = Operate
- HOT requests sent at 10 Hz during reposition (vs AGL-based gating in normal flight)
- HOT responses only accepted when SOF IG Status = Operate (terrain loaded)
- X-Plane plugin: detects Reset→Operate transition, probes terrain stability (4×0.5s),
  reports SOF Standby during probing, Normal when stable

**Startup note:** Kill stale cigi_bridge processes before starting (`fuser -k 8001/udp 8002/udp`).
A zombie process holding port 8001 causes position flicker (both old and new process send Entity Control).

---

## QTG Engine (`src/qtg/engine/`)

- Maneuver scripts: YAML in `src/qtg/maneuvers/`
- Sim Manager executes maneuver, records ROS2 bag
- Engine extracts parameters, compares to reference band
- Generates PDF report to `src/qtg/reports/`

---

## Build

Always from workspace root:
```bash
cd ~/simulator_framework
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install
source install/setup.bash
```

Build a single package:
```bash
colcon build --packages-select sim_msgs
source install/setup.bash
```

---

## What NOT to Do

- Never call flight model code directly — always go through `/aircraft/fdm/state`
- Never hard-code aircraft parameters in node code — always read from aircraft YAML
- Never cross-subscribe systems nodes except for documented physical dependencies
  (engines→electrical, engines→fuel, air_data→electrical)
- Never subscribe to `/sim/failures/active` — doesn't exist. Broadcast is `/sim/failures/state`,
  injection is `/sim/failures/route/<handler>`.
- Never let a sim node subscribe to `/aircraft/devices/*` — only input_arbitrator reads those
- IOS backend publishes only to `/aircraft/devices/instructor/*` and `/sim/command` —
  NEVER to `/sim/*/state` or `/aircraft/devices/virtual/*`
- Never put IOS logic in Sim Manager — IOS sends commands, Sim Manager executes
- Never store sim state in ios_backend — it's stateless, ROS2 is the source of truth
- Never compute atmosphere, signal reception, or terrain in a systems node — `/world/` only
- Never use bare `_freq` suffix on avionics fields — `_freq_mhz` or `_freq_khz`
- Never hardcode a node list in ios_backend — node discovery is fully dynamic
- Never use `rclpy.spin()` in a thread under uvicorn — use `spin_once()` loop
- Never `json.dumps()` ROS2 message data — use `_dumps()` with `_RosEncoder` (numpy types)
- Never start ios_backend via sim_full.launch.py — manual terminal only
- Never put a ROS2 package outside `src/`

---

## Open Decisions

Resolved decisions live in DECISIONS.md. Still open:

- [ ] CIGI library: from scratch or cigicl? (currently raw encoding, no CCL)
- [ ] micro-ROS transport: serial UART or CAN?
- [ ] IOS auth: single-user or multi-role (instructor / examiner / admin)?
- [ ] Scenario file format: custom YAML or existing standard?
- [ ] IG manager: lifecycle node on remote hardware for spawning OpenGL IGs?

---

## Coding Guidelines

Behavioral guidelines to reduce common LLM coding mistakes. Merge with
project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial
tasks, use judgment.

### 1. Think Before Coding
Don't assume. Don't hide confusion. Surface tradeoffs.

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them — don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

### 2. Simplicity First
Minimum code that solves the problem. Nothing speculative.

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes,
simplify.

### 3. Surgical Changes
Touch only what you must. Clean up only your own mess.

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it — don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: every changed line should trace directly to the user's request.

### 4. Goal-Driven Execution
Define success criteria. Loop until verified.

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:

```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it
work") require constant clarification.

These guidelines are working if: fewer unnecessary changes in diffs, fewer
rewrites due to overcomplication, and clarifying questions come before
implementation rather than after mistakes.