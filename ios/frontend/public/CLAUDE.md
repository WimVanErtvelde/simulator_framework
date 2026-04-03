# CLAUDE.md — Simulator Framework

This file is the working memory for Claude Code sessions.
Read this at the start of every session before touching any code.
Update it whenever architectural decisions are made or changed.

---

## Startup Sequence

Three terminals required. Start in order:

**Terminal 1 — Simulator**
```bash
cd ~/simulator_framework
source /opt/ros/jazzy/setup.bash && source install/setup.bash
ros2 launch launch/sim_full.launch.py
```

**Terminal 2 — IOS Backend** (wait until sim prints "All N required nodes alive")
```bash
cd ~/simulator_framework
source /opt/ros/jazzy/setup.bash && source install/setup.bash
python3 -m uvicorn ios_backend.ios_backend_node:app --host 0.0.0.0 --port 8080
```
Wait ~10 seconds after startup for DDS peer discovery before checking node status.

**Terminal 3 — IOS Frontend**
```bash
cd ~/simulator_framework/ios/frontend
npm run dev
# → http://localhost:5173
```

**Notes**
- Backend MUST be started with ROS2 sourced — without it, DDS isolation means no nodes are visible
- Backend is NOT in sim_full.launch.py (removed — it conflicts on port 8080 if already running)
- Kill stale backend: `fuser -k 8080/tcp`
- Kill stale frontend: `fuser -k 5173/tcp`

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

1. **Swappable Flight Model** — the flight model is behind an adapter interface. The rest of the sim never calls flight model code directly.
2. **Swappable IG** — visual system is decoupled via CIGI. Any CIGI-compliant IG can be used.
3. **ROS2 as the backbone** — all systems communicate via ROS2 topics. No direct cross-node function calls.
4. **Systems nodes never talk to each other directly** — they subscribe to `/aircraft/fdm/state` and publish their own output. Coupling goes through the Flight Model Adapter or the Sim Manager only.
5. **Input arbitration** — all control inputs (hardware, virtual panels, instructor override) are arbitrated by the Input Arbitrator node before reaching the sim. No sim node ever reads device topics directly. The arbitrator is the single source of truth for all control inputs, with per-channel source selection configurable at runtime via IOS.
6. **Aircraft config drives everything** — which nodes load, which Flight Model Adapter runs, which instrument panels show, all driven by YAML config per aircraft type.
7. **IOS is purely a control surface** — it injects commands via ROS2 topics. It has no privileged access to sim internals.
8. **World environment is shared infrastructure** — atmosphere, weather, nav signals, and terrain are published under `/world/` and consumed by any node that needs them. Systems nodes never compute environmental state themselves.

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
├── docker-compose.yml
├── src/                             ← ALL ROS2 packages (colcon discovers recursively)
│   ├── core/
│   │   ├── sim_manager/             ← ROS2 node: sim clock, state machine, lifecycle mgmt
│   │   ├── flight_model_adapter/    ← ROS2 node: IFlightModelAdapter interface + implementations
│   │   ├── input_arbitrator/        ← ROS2 node: per-channel source selection
│   │   ├── atmosphere_node/         ← ROS2 node: ISA + weather deviation → /world/atmosphere
│   │   ├── navaid_sim/              ← ROS2 node: VOR/ILS/NDB/DME/markers, DTED LOS, A424+XP format
│   │   ├── cigi_bridge/             ← ROS2 node: CIGI host implementation
│   │   └── sim_interfaces/          ← headers-only: shared C++ interfaces (no node)
│   ├── systems/
│   │   ├── electrical/              ← ROS2 node: DC/AC buses, breakers (pluginlib, ElectricalSolver)
│   │   ├── fuel/                    ← ROS2 node: tanks, feed, transfer, CG
│   │   ├── hydraulic/               ← ROS2 node: system pressures
│   │   ├── navigation/              ← ROS2 node: onboard receivers (VOR/ILS/GPS/ADF) — no pluginlib
│   │   ├── engine_systems/          ← ROS2 node: N1/N2, EGT, torque, start sequence
│   │   ├── failures/                ← ROS2 node: failure injector, active failure broadcast
│   │   ├── ice_protection/          ← ROS2 node: de-ice, anti-ice, pitot heat
│   │   ├── pressurization/          ← ROS2 node: applicable aircraft only
│   │   └── gear/                    ← ROS2 node: retract/extend, WoW, warnings
│   ├── hardware/
│   │   └── microros_bridge/         ← ROS2 node: serial/CAN → /aircraft/devices/hardware/
│   ├── ios_backend/                 ← ROS2 ament_python package: FastAPI + rclpy IOS bridge
│   ├── aircraft/
│   │   ├── c172/                    ← ROS2 package: config YAML, flight model data, panel layout, plugins
│   │   └── ec135/                   ← ROS2 package: rotary-wing example
│   ├── sim_msgs/                    ← ROS2 package: all custom message definitions
│   └── qtg/
│       └── engine/                  ← ROS2 ament_python package: test runner + QTG reports
├── ios/
│   └── frontend/                    ← React IOS web app (NOT a ROS2 package)
│       ├── src/
│       │   ├── main.jsx             ← React Router entry (/ = IOS, /cockpit/c172/* = virtual cockpit)
│       │   ├── store/useSimStore.js  ← Zustand store, WebSocket handler
│       │   └── components/          ← StatusStrip, NavTabs, MapView, panels/, cockpit/
│       └── package.json
├── launch/
│   └── sim_full.launch.py           ← launches all sim nodes (NOT ios_backend — run manually)
└── tools/
    └── scenario_editor/             ← optional GUI scenario builder
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
                                        ↓
                                    SHUTDOWN
```

- Loads aircraft config YAML on startup (search path: parameter → `src/aircraft/<id>/config/config.yaml` → installed share)
- Spawns/tears down system nodes based on aircraft config
- Exposes a ROS2 service interface for IOS commands
- Heartbeat monitoring: 2-second timeout, auto-freeze on required node loss
- RESETTING state: 100ms wall timer before transitioning to READY (gives nodes time to receive IC broadcast)

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

**Implementations (under `src/core/flight_model_adapter/`):**
- `JSBSimAdapter` — wraps JSBSim via C++ API (FetchContent v1.2.1 static lib)
- `XPlaneUDPAdapter` — connects to X-Plane via UDP data
- `HelisimUDPAdapter` — connects to Helisim 6.0 via UDP ICD (doc 743-0507)
- `CustomCertifiedAdapter` — placeholder for authority-certified flight model

**JSBSim notes:**
- Uses wall timer (not sim timer) for its 50Hz update loop — drives JSBSim independently
- Default IC: EBBR rwy 25L, airborne_clean at 2500ft / 90kt / HDG 040°
- Simple PID autopilot auto-engages on airborne_clean IC for pipeline testing
- Engine start uses `propulsion/set-running` (not per-engine — initializes magnetos correctly)
- FlightModelState carries position in ECG (lat/lon/alt) AND ECEF (x/y/z); velocity in NED, Body, and ECEF frames

**Publishes:** `/aircraft/fdm/state` (FlightModelState — position, attitude, velocities, accelerations, aero forces, WoW)

---

## ROS2 Topic Conventions

### Naming rule

Three roots:
- `/world/` — environment (weather, navaids, terrain, traffic)
- `/aircraft/` — the simulated machine (FDM, systems, controls, input devices)
- `/sim/` — simulation infrastructure (state machine, diagnostics, CIGI, failures)

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
| `/sim/hydraulic/state` | HydraulicState | sim_hydraulic | System pressures |
| `/aircraft/navigation/state` | NavigationState | sim_navigation | Onboard receiver outputs: VOR, ILS, GPS, ADF, DME, TACAN |
| `/aircraft/engines/state` | EngineState | sim_engine_systems | N1/N2, EGT, torque |
| `/sim/failures/state` | FailureList | sim_failures | Broadcast to all nodes |
| `/sim/alerts` | SimAlert | any node | SEVERITY_INFO/WARN/CRITICAL alerts to IOS |
| `/sim/cigi/host_to_ig` | CigiPacket | cigi_bridge | Host → IG packets |
| `/sim/cigi/ig_to_host` | CigiPacket | cigi_bridge | IG → Host packets |

### Diagnostics topics

| Topic | Type | Publisher | Notes |
|---|---|---|---|
| `/sim/diagnostics/heartbeat` | std_msgs/String | each node | Node name as data, published at 1 Hz |
| `/sim/diagnostics/lifecycle` | std_msgs/String | each node | Format: "node_name:state" on every transition |

### World environment topics

| Topic | Type | Publisher | Notes |
|---|---|---|---|
| `/world/atmosphere` | AtmosphereState | atmosphere_node | Pressure, temp, density at aircraft position |
| `/world/weather` | WeatherState | sim_manager | Wind layers, turbulence, vis, precip, icing |
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

Channels: `flight`, `engine`, `avionics`, `panel` (4 channels total).

Hardware timeout > 500ms → auto-fallback to VIRTUAL + alert on `/aircraft/controls/arbitration`.

Instructor takeover is **sticky** — once instructor publishes on a channel, source stays INSTRUCTOR
until node reconfigure. No auto-release, no timeout.

---

## IOS Command Architecture

**Three tiers, never cross them:**

| Source | Topic | Priority | Who publishes |
|---|---|---|---|
| IOS A/C page switches | `/aircraft/devices/instructor/panel` | INSTRUCTOR | ios_backend |
| IOS frequency tuning | `/aircraft/devices/instructor/controls/avionics` | INSTRUCTOR | ios_backend |
| Virtual cockpit switches | `/aircraft/devices/virtual/panel` | VIRTUAL | cockpit browser pages |
| Virtual cockpit avionics | `/aircraft/devices/virtual/controls/avionics` | VIRTUAL | cockpit browser pages |
| Physical hardware | `/aircraft/devices/hardware/panel` | HARDWARE | microros_bridge |

IOS A/C page switches are instructor-level by default — the act of the instructor touching
a switch IS the force. No separate "Force" button needed. Amber styling on IOS switches
communicates instructor authority visually.

All panel UIs read displayed state from `/aircraft/controls/panel` (arbitrated output) — never
from their own published commands. Single source of truth.

---

## IOS Backend (`src/ios_backend/`)

FastAPI + rclpy. Bridges ROS2 ↔ WebSocket ↔ React frontend.
Run manually (not via launch file) so output is visible and it can be restarted independently.

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
CMD_RUN             = 1
CMD_FREEZE          = 2
CMD_RESET_FLIGHT    = 3
CMD_RESET_AIRCRAFT  = 4
CMD_RESET_FAILURES  = 5
CMD_SHUTDOWN        = 6
CMD_RELOAD_NODE     = 7   # payload: {node_name}
CMD_DEACTIVATE_NODE = 8   # payload: {node_name}
CMD_ACTIVATE_NODE   = 9   # payload: {node_name}
CMD_RESET_NODE      = 10  # payload: {node_name} — chains deactivate→cleanup→configure→activate
```

**Topic subscriptions:** `/aircraft/fdm/state`, `/sim/state`, `/aircraft/fuel/state`, `/aircraft/navigation/state`,
`/aircraft/controls/avionics`, `/aircraft/electrical/state`, `/sim/alerts`, `/sim/diagnostics/heartbeat`,
`/sim/diagnostics/lifecycle`

**Topic publishers:** `/aircraft/devices/instructor/panel`, `/aircraft/devices/instructor/controls/avionics`,
`/aircraft/devices/virtual/panel` (for cockpit pages via separate WS handler)

---

## IOS Frontend (`ios/frontend/`)

React + Zustand + WebSocket + React Router. Served by Vite dev server on port 5173.
**Not a ROS2 package** — lives outside `src/`.

**URL routing:**
- `/` — main IOS app (map, status strip, 9 panel tabs, action bar)
- `/cockpit/c172/electrical` — virtual C172 electrical panel (VIRTUAL priority switches)
- `/cockpit/c172/avionics` — virtual C172 avionics panel (placeholder)

**Key files:**
- `src/main.jsx` — React Router entry point
- `src/store/useSimStore.js` — Zustand store, WebSocket message handler, CMD dispatch
- `src/components/StatusStrip.jsx` — top 3-row status (Row 3 = dynamic radio row from navigation.yaml)
- `src/components/NavTabs.jsx` — 9 left-side navigation tabs (56px wide, monospace symbols)
- `src/components/MapView.jsx` — Leaflet map, type-aware aircraft icon (color = sim state)
- `src/components/panels/NodesPanel.jsx` — dynamic node discovery, lifecycle state, per-node controls
- `src/components/panels/AircraftPanel.jsx` — fully dynamic A/C page, driven by navigation.yaml
- `src/components/cockpit/CockpitElectrical.jsx` — virtual cockpit electrical panel (VIRTUAL)

**Design palette:** bg `#0a0e17`, panel `#111827`, elevated `#1c2333`, borders `#1e293b`,
text `#e2e8f0`, dim `#64748b`, accent `#00ff88`, cyan `#39d0d8`, danger `#ff3b30`.
IOS switches: amber (instructor). Virtual cockpit switches: green (standard).

---

## Systems Nodes

Each system node follows this pattern:
- Uses ROS2 sim time (`use_sim_time: true`) — timer driven by `/clock`
- Implemented as `rclcpp_lifecycle::LifecycleNode`
- Auto-activates on startup via `trigger_transition()` in a 100ms timer
- Subscribes to `/aircraft/fdm/state`, `/sim/failures/state`, relevant `/sim/controls/` and `/world/` topics
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
`sim_electrical`, `sim_fuel`, `sim_gear`

**Nodes NOT using pluginlib** (aircraft-agnostic):
`sim_navigation` — receiver behavior is standard across aircraft types

---

## navaid_sim (`src/core/navaid_sim/`)

Core framework package. Ground navaid environment node.

- Subscribes to `/aircraft/fdm/state` (position) and `/aircraft/controls/avionics` (frequencies, OBS)
- Publishes `/world/nav_signals` (NavSignalTable.msg) at 10 Hz
- Data source selected via ROS2 YAML parameter `data_source`:
  - `"euramec"` — EURAMEC.PC (navaids + markers + magnetic deviation in one file)
  - `"xplane"` — earth_nav.dat (XP810/XP12 auto-detected by token count) + WMM.COF
- Data files installed to `share/navaid_sim/data/`
- SRTM terrain LOS checks on all VHF receivers
- Startup log: count of VORs, ILS, NDBs, DMEs, markers loaded + source file

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
- `float64` for lat/lon only
- `float32` for all other numeric fields
- `bool` for flags, `string` for idents

**NavigationState** includes frequency echoes (`com1/2/3_freq_mhz`, `adf1/2_freq_khz`) from
AvionicsControls for IOS display convenience.

**Extended fields** (future-proof, all zero until implemented):
- COM3, ADF2, TACAN (channel + band), GPS source selector (GPS1/GPS2)
- GPS2 receiver (full lat/lon/alt/gs/track/valid)
- TACAN bearing/distance/valid

**Installed avionics per aircraft** defined in `src/aircraft/<type>/config/navigation.yaml`.
IOS A/C page and StatusStrip Row 3 render dynamically from this config.

---

## Aircraft Configuration

Each aircraft: `src/aircraft/<type>/` — contains `package.xml`, `CMakeLists.txt`, `plugins.xml`,
`src/` (plugin implementations), and `config/` (per-system YAML files).

Config files per aircraft:
- `config/config.yaml` — metadata, required nodes, Flight Model Adapter, limits, default IC
- `config/electrical.yaml` — bus topology, sources, loads, switch IDs
- `config/fuel.yaml` — tanks, selectors, pumps
- `config/navigation.yaml` — installed avionics equipment (drives dynamic IOS A/C page)

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

- CIGI 3.3 / 4.0 host side
- Subscribes to `/aircraft/fdm/state`, `/world/weather`, `/sim/scenario/entities`
- Publishes `/sim/cigi/host_to_ig` and `/sim/cigi/ig_to_host`
- IG identity hidden from rest of sim

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

- Never call flight model code directly from a systems node — always go through `/aircraft/fdm/state`
- Never hard-code aircraft parameters in node code — always read from aircraft YAML config
- Never let system nodes subscribe to each other — all coupling via `/aircraft/fdm/state`, `/sim/failures/state`, or `/world/`
- Never let any sim node subscribe to `/aircraft/devices/` topics — only input_arbitrator reads device topics
- Never let ios_backend publish to `/sim/` topics directly — it publishes to `/aircraft/devices/instructor/` only
- Never put IOS logic in Sim Manager — IOS sends commands, Sim Manager executes them
- Never store sim state in the IOS backend — it is stateless, ROS2 is the source of truth
- Never put a ROS2 package outside `src/` — if it has a `package.xml` it belongs under `src/`
- Never start ios_backend via sim_full.launch.py — run it manually in its own terminal
- Never compute atmosphere, signal reception, or terrain in a systems node — subscribe to `/world/` instead
- Never use a bare `_freq` suffix on avionics message fields — always `_freq_mhz` or `_freq_khz`
- Never publish IOS A/C page commands to `/aircraft/devices/virtual/` — always `/aircraft/devices/instructor/`
- Never hardcode a node list in ios_backend — node discovery is fully dynamic

---

## Open Decisions

- [x] Sim clock: ROS2 native sim time (`/clock` + `use_sim_time`) ✓
- [x] Nav signals: `/world/` namespace, navaid_sim as core package ✓
- [x] NavSignalTable interface: finalised ✓
- [x] Virtual panel rendering: web-based (React, WebSocket → FastAPI → ROS2) ✓
- [x] CGF: scripted entities in scenario files first; live panel deferred ✓
- [x] Workspace layout: all ROS2 packages under `src/`, frontend outside ✓
- [x] ios_backend excluded from launch file — run manually ✓
- [x] IOS panel priority: instructor-level by default ✓
- [x] Virtual cockpit pages: VIRTUAL priority, URL-routed via React Router ✓
- [ ] CIGI library: from scratch or cigicl?
- [ ] micro-ROS transport: serial UART or CAN?
- [ ] IOS auth: single-user or multi-role (instructor / examiner / admin)?
- [ ] Scenario file format: custom YAML or existing standard?
- [ ] Terrain service: separate node or merged into navaid_sim?
- [ ] IG manager: lifecycle node on remote hardware (e.g. Raspberry Pi) to spawn/monitor OpenGL IG executables