# CLAUDE.md — Simulator Framework

This file is the working memory for Claude Code sessions.
Read this at the start of every session before touching any code.
Update it whenever architectural decisions are made or changed.

---

## Project Goal

A **reusable, modular flight simulator framework** targeting FNPT II and FTD Level 2 qualification.
Designed to support both **fixed-wing and rotary-wing** aircraft.

The framework itself does not need to be certified — only the flight model data and its
validation output (QTG) need to meet authority requirements.

---

## Core Design Principles

1. **Swappable FDM** — the flight model is behind an adapter interface. The rest of the sim never calls FDM code directly.
2. **Swappable IG** — visual system is decoupled via CIGI. Any CIGI-compliant IG can be used.
3. **ROS2 as the backbone** — all systems communicate via ROS2 topics. No direct cross-node function calls.
4. **Systems nodes never talk to each other directly** — they subscribe to `/sim/fdm/state` and publish their own output. Coupling goes through the FDM adapter or the Sim Manager only.
5. **Input arbitration** — all control inputs (hardware, virtual panels, instructor override) are arbitrated by the Input Arbitrator node before reaching the sim. No sim node ever reads device topics directly. The arbitrator is the single source of truth for all control inputs, with per-channel source selection configurable at runtime via IOS.
6. **Aircraft config drives everything** — which nodes load, which FDM adapter runs, which instrument panels show, all driven by YAML config per aircraft type.
7. **IOS is purely a control surface** — it injects commands via ROS2 topics. It has no privileged access to sim internals.
8. **World environment is shared infrastructure** — atmosphere, weather, nav signals, and terrain are published under `/sim/world/` and consumed by any node that needs them. Systems nodes never compute environmental state themselves.

---

## Tech Stack

| Layer | Technology |
|---|---|
| Middleware | ROS2 Jazzy (LTS) |
| Systems nodes | C++ (real-time, deterministic) |
| FDM adapter | C++ with abstract plugin interface |
| IOS backend | Python / FastAPI + rclpy |
| IOS frontend | React + Zustand + WebSocket |
| Virtual panels | Rendering TBD (web, OpenGL, or VR/OpenXR — see Open Decisions) |
| Hardware MCUs | ESP32 or STM32 with micro-ROS |
| micro-ROS agent | ROS2 node bridging serial/CAN to ROS2 topics |
| Aircraft config | YAML |
| Replay | ROS2 bag (native) |
| QTG reports | Python + matplotlib + ReportLab |
| CIGI | CIGI 3.3 / 4.0 (host-side implementation in C++) |

---

## Repository Structure

```
simulator_framework/
├── CLAUDE.md                        ← this file
├── core/
│   ├── sim_manager/                 ← ROS2 node, owns sim clock and state machine
│   ├── flight_model_adapter/        ← abstract interface + FDM implementations
│   ├── input_arbitrator/            ← ROS2 node, per-channel source selection
│   ├── atmosphere_node/             ← ROS2 node, ISA + weather deviation → /sim/world/atmosphere
│   └── cigi_bridge/                 ← ROS2 node, CIGI host implementation
├── systems/
│   ├── electrical/                  ← ROS2 node
│   ├── fuel/                        ← ROS2 node
│   ├── hydraulic/                   ← ROS2 node
│   ├── navigation/                  ← ROS2 node (onboard receivers only — consumes /sim/world/nav_signals)
│   ├── engines/                     ← ROS2 node
│   ├── failures/                    ← ROS2 node, broadcasts active failures
│   ├── ice_protection/              ← ROS2 node
│   ├── pressurization/              ← ROS2 node (applicable aircraft only)
│   └── gear/                        ← ROS2 node
├── hardware/
│   ├── microros_bridge/             ← ROS2 node, publishes to /devices/hardware/
│   └── virtual_panels/              ← rendering TBD, publishes to /devices/virtual/
├── ios/
│   ├── backend/                     ← FastAPI + rclpy, publishes to /devices/instructor/
│   └── frontend/                    ← React IOS web app
├── qtg/
│   ├── engine/                      ← automated test runner + data extractor
│   ├── maneuvers/                   ← test maneuver scripts (YAML)
│   └── reports/                     ← generated QTG PDF output
├── aircraft/
│   ├── c172/                        ← config YAML, FDM reference data, panel layout
│   └── ec135/                       ← rotary-wing example
├── sim_msgs/                        ← all custom ROS2 message definitions
└── tools/
    └── scenario_editor/             ← optional: GUI scenario builder
```

---

## Sim Manager

**The heartbeat of the framework.**

- Uses ROS2 native sim time (`/clock` topic + `use_sim_time: true` on all nodes)
- All ROS2 tooling (rosbag, rviz, rqt) understands sim time natively — no custom clock needed
- Sim Manager owns and drives the `/clock` topic at fixed rate (50 Hz default, configurable)
- Manages sim state machine:

```
INIT → READY → RUNNING ↔ PAUSED → REPLAY
                  ↓
               FROZEN (position / fuel / attitude flags)
```

- Loads aircraft config YAML on startup
- Spawns/tears down system nodes based on aircraft config
- Exposes a ROS2 service interface for IOS commands
- Injects weather state from IOS/scenario commands into `/sim/world/weather`
- Handles scenario loading and ROS2 bag record/playback for replay

---

## Flight Model Adapter

Abstract C++ interface — never instantiate a concrete FDM outside this adapter.

```cpp
class IFlightModel {
public:
    virtual void init(const AircraftConfig& config) = 0;
    virtual FdmState step(double dt, const ControlsInput& controls) = 0;
    virtual void setInitialConditions(const InitialConditions& ic) = 0;
    virtual SimCapabilities getCapabilities() = 0;  // fixed_wing, helicopter, etc.
};
```

**Implementations (under `flight_model_adapter/`):**
- `JSBSimAdapter` — wraps JSBSim via its C++ API
- `XPlaneUDPAdapter` — connects to X-Plane via UDP data
- `CustomCertifiedAdapter` — placeholder for authority-certified FDM

**Publishes:** `/sim/fdm/state` (position, attitude, velocities, accelerations, aero forces, weight on wheels)

---

## ROS2 Topic Conventions

### Naming rule

Two roots, no exceptions:
- `/devices/` — anything produced by an external source (hardware, virtual panels, instructor)
- `/sim/` — anything produced or consumed internally by the simulator

All topics use `snake_case`. No abbreviations unless universally understood (e.g. `fdm`, `cigi`).

### Device topics (raw inputs — never read by sim nodes directly)

| Topic | Type | Publisher | Notes |
|---|---|---|---|
| `/devices/hardware/controls/flight` | RawFlightControls | microros_bridge | Yoke, pedals, collective from MCU |
| `/devices/hardware/controls/engine` | RawEngineControls | microros_bridge | Throttle, mixture, condition from MCU |
| `/devices/hardware/controls/avionics` | RawAvionicsControls | microros_bridge | Radio, nav, transponder from MCU |
| `/devices/hardware/heartbeat` | DeviceHeartbeat | microros_bridge | Per-channel health, used by arbitrator |
| `/devices/virtual/controls/flight` | RawFlightControls | virtual_panels | Same type as hardware equivalent |
| `/devices/virtual/controls/engine` | RawEngineControls | virtual_panels | Same type as hardware equivalent |
| `/devices/virtual/controls/avionics` | RawAvionicsControls | virtual_panels | Same type as hardware equivalent |
| `/devices/instructor/controls/flight` | RawFlightControls | ios_backend | Instructor takeover input |
| `/devices/instructor/controls/engine` | RawEngineControls | ios_backend | Instructor engine override |

### Arbitrated sim topics (what the sim actually reads)

| Topic | Type | Publisher | Notes |
|---|---|---|---|
| `/clock` | rosgraph_msgs/Clock | sim_manager | ROS2 native sim time — drives all nodes |
| `/sim/state` | SimState | sim_manager | INIT, READY, RUNNING, PAUSED, REPLAY |
| `/sim/fdm/state` | FdmState | flight_model_adapter | Position, attitude, velocities, forces |
| `/sim/controls/flight` | FlightControls | input_arbitrator | Authoritative flight controls output |
| `/sim/controls/engine` | EngineControls | input_arbitrator | Authoritative engine controls output |
| `/sim/controls/avionics` | AvionicsControls | input_arbitrator | Authoritative avionics controls output |
| `/sim/controls/arbitration/state` | ArbitrationState | input_arbitrator | Per-channel source (HARDWARE/VIRTUAL/INSTRUCTOR/FROZEN) |
| `/sim/electrical/state` | ElectricalState | electrical_node | DC/AC buses, breakers |
| `/sim/fuel/state` | FuelState | fuel_node | Quantities, flow, CG |
| `/sim/hydraulic/state` | HydraulicState | hydraulic_node | System pressures |
| `/sim/nav/state` | NavState | navigation_node | Onboard receiver outputs: VOR, ILS, GPS, ADF |
| `/sim/engines/state` | EngineState | engines_node | N1/N2, EGT, torque |
| `/sim/failures/active` | FailureList | failures_node | Broadcast to all nodes |
| `/sim/cigi/host_to_ig` | CigiPacket | cigi_bridge | Host → IG packets |
| `/sim/cigi/ig_to_host` | CigiPacket | cigi_bridge | IG → Host packets |

### World environment topics

The physical world surrounding the aircraft. Published by framework nodes or external services.
Systems nodes that need environmental data subscribe here — never compute it themselves.

| Topic | Type | Publisher | Notes |
|---|---|---|---|
| `/sim/world/atmosphere` | AtmosphereState | atmosphere_node | Pressure, temp, density at aircraft position (ISA + deviations) |
| `/sim/world/weather` | WeatherState | sim_manager | Injected met environment: wind layers, turbulence, vis, precip, icing |
| `/sim/world/nav_signals` | NavSignalTable | nav_sim (external) | Receivable navaids, signal strength, LOS status — see nav_sim project |
| `/sim/world/terrain` | TerrainState | terrain_node (TBD) | DTED-derived elevation and obstruction data at aircraft position |

---

## World Environment Architecture

### Atmosphere node (`core/atmosphere_node`)
Computes the physical atmosphere at the aircraft's current position:
- Subscribes to `/sim/fdm/state` (for altitude) and `/sim/world/weather` (for OAT/pressure deviations)
- Computes ISA baseline + instructor-injected deviations
- Publishes `/sim/world/atmosphere` at sim rate
- Consumed by: engines_node (density altitude, OAT), altimeter (pressure altitude), ice_protection_node (SAT)

### Weather (sim_manager owned)
- Weather state is set by IOS commands or scenario scripts
- Sim Manager publishes `/sim/world/weather` when IOS injects or scenario changes weather
- Consumed by: atmosphere_node, cigi_bridge (IG visual weather), ice_protection_node

### Nav signals (external — nav_sim project)
- Separate repository and process
- Loads navaid database (Jeppesen or X-Plane `earth_nav.dat`)
- Loads DTED tiles, performs line-of-sight ray casting from navaid to aircraft
- Computes signal strength, bearing error, glide slope deviation, marker passage
- Publishes `/sim/world/nav_signals` into the framework via ROS2 topic
- The `navigation_node` in systems subscribes to this — it never computes signal reception itself
- Interface message type: to be defined when nav_sim project interface is finalised

### Terrain (TBD)
- DTED loading and proximity queries
- May be folded into nav_sim project or implemented as a separate node
- Open decision — defer until needed

---

## Systems Nodes

Each system node follows this pattern:
- Nodes use ROS2 sim time (`use_sim_time: true`). Timer callbacks are driven automatically
  by the `/clock` topic published by Sim Manager — no explicit clock subscription needed.
- Subscribes to `/sim/fdm/state`, `/sim/failures/active`, relevant `/sim/controls/` topics,
  and relevant `/sim/world/` topics as needed
- Publishes its `/sim/<system>/state` topic
- Reads its configuration from the aircraft YAML on init
- Respects freeze flags published by Sim Manager on `/sim/state`

**Nodes planned:**
- `electrical_node` — DC/AC buses, battery, alternators, circuit breakers
- `fuel_node` — tanks, feed, transfer, CG, freeze support
- `hydraulic_node` — system A/B, accumulator, emergency
- `navigation_node` — onboard receivers only (VOR, ILS, GPS, ADF, transponder); consumes `/sim/world/nav_signals`
- `engines_node` — N1/N2, EGT, torque, fuel flow, start sequence; consumes `/sim/world/atmosphere`
- `failures_node` — central failure injector, reads IOS commands, broadcasts active failures
- `ice_protection_node` — de-ice, anti-ice, pitot heat; consumes `/sim/world/atmosphere` and `/sim/world/weather`
- `pressurization_node` — applicable aircraft only
- `gear_node` — retract/extend, weight on wheels, warnings

---

## Input Arbitrator

**The single source of truth for all control inputs.**

All hardware, virtual panel, and instructor inputs flow through here before the sim sees them.
No sim node subscribes to `/devices/` topics directly — ever.

### Source priority (per channel)
```
INSTRUCTOR  (highest — explicit IOS takeover)
HARDWARE    (real MCU input, when healthy)
VIRTUAL     (virtual panel fallback)
FROZEN      (hold last value, ignore all input)
```

### Per-channel source selection
Each control channel (e.g. aileron, elevator, throttle_1, mixture_1) has an independent
source setting. This allows mixed configs: real yoke + virtual throttle quadrant.

### Hardware health monitoring
The arbitrator watches `/devices/hardware/heartbeat` per channel.
Timeout > 500ms → auto-fallback to VIRTUAL + alert published to `/sim/controls/arbitration/state`.
Instructor can see hardware health in IOS and manually override any channel at any time.

### Data flow
```
/devices/hardware/controls/flight   ──┐
/devices/virtual/controls/flight   ───┼──► input_arbitrator ──► /sim/controls/flight
/devices/instructor/controls/flight ──┘          │
                                                  └──► /sim/controls/arbitration/state
```

---

## CIGI Bridge

- Implements CIGI 3.3 / 4.0 host side
- Subscribes to `/sim/fdm/state`, `/sim/world/weather`, `/sim/scenario/entities`
- Sends to IG: entity position/attitude, atmosphere, time-of-day, special effects, weather visuals
- Receives from IG: line-of-sight responses, collision events
- Publishes raw packets on `/sim/cigi/host_to_ig` and `/sim/cigi/ig_to_host`
- IG identity is completely hidden from the rest of the sim

---

## Hardware Bridge & Virtual Panels

### Hardware bridge (micro-ROS path)
```
MCU (ESP32/STM32)
  └── micro-ROS firmware
        └── serial / CAN
              └── micro-ROS agent (ROS2 node)
                    └── /devices/hardware/controls/*
```

### Virtual panels — interface contract
Virtual panels are defined by their **ROS2 topic interface only**. Rendering implementation is deferred.

A virtual panel implementation must:
- Subscribe to relevant `/sim/` state topics to drive instrument display (needles, flags, annunciators)
- Publish pilot inputs to `/devices/virtual/controls/*`
- Have no knowledge of how other sim nodes work

**Rendering options under consideration (not yet decided):**
- Web-based (React + WebSocket → IOS backend → ROS2) — good for software testing, tablet IOS
- OpenGL native app (subscribes to ROS2 directly) — good for low-latency, full cockpit panels
- VR (OpenGL/Vulkan, e.g. OpenXR) — required for immersive VR cockpit scenario

These are not mutually exclusive. Multiple renderers can run simultaneously on the same topics
(e.g. web panel for instructor + VR cockpit for pilot). Rendering choice is an open decision.

### Design rule
Virtual panels and hardware bridge publish on **structurally identical message types**
but **different topic paths** (`/devices/virtual/` vs `/devices/hardware/`).
The arbitrator selects between them — the rest of the sim never knows which is active.

---

## IOS — Instructor Operating Station

Web-based, runs in any browser, served by the IOS backend.

**Features:**
- Sim control: run, pause, reset, shutdown
- Freeze controls: position freeze, fuel freeze, attitude freeze
- Initial conditions: position, heading, speed, altitude
- Weather injection: wind (dir/speed/shear/turb), visibility, cloud layers, OAT
- Failure injection: browse failure tree, inject/clear per system
- Scenario management: load, start, stop `.scenario` YAML files
- Replay: play back recorded ROS2 bags with timeline scrubbing
- Debriefing: replay + parameter plots (altitude, speed, heading, VS over time)
- QTG creation: run automated test maneuvers, generate compliance report PDF
- Hardware status panel: per-channel arbitration state, health indicators

**Backend:** FastAPI + `rclpy`, exposes REST + WebSocket
**Frontend:** React + Zustand for state, WebSocket for live data

---

## QTG Engine

Automated qualification test capability.

- Test maneuvers defined as YAML scripts (e.g. `static_stability_pitch.yaml`)
- Sim Manager executes maneuver autonomously, records ROS2 bag
- QTG engine extracts parameters from bag, compares to reference data band
- Reference data stored in aircraft config (from manufacturer data or flight test)
- Generates PDF report with:
  - Test identification
  - Parameter time-history plots
  - Pass/fail against reference band
  - Test conditions (weight, CG, altitude, speed, config)

---

## Aircraft Configuration (YAML)

Each aircraft lives in `aircraft/<type>/config.yaml`.

Defines:
- Aircraft metadata (type, ICAO, category)
- Which system nodes to load
- FDM adapter to use + FDM config path
- Instrument panel layout (which virtual panels to show)
- QTG reference data paths
- Limits (Vne, Vfe, max torque, etc.)

---

## External Services

Services that run as separate processes/repos but interface with the framework via ROS2 topics.
The framework runs without them — subscribing nodes handle absent topics gracefully.

| Service | Topic(s) | Notes |
|---|---|---|
| `nav_sim` | `/sim/world/nav_signals` | VOR/ILS/NDB/Markers with DTED LOS. Separate repo. |
| `terrain_service` | `/sim/world/terrain` | DTED elevation queries. May merge with nav_sim. TBD. |

---

## Build Order (implementation sequence)

1. `CLAUDE.md` ← you are here
2. ROS2 package scaffold (all packages, no logic yet)
3. Sim Manager — clock + state machine only
4. Flight Model Adapter — interface + JSBSim implementation
5. Atmosphere node — ISA model + weather deviation input
6. Input Arbitrator — source selection, health monitoring, freeze support
7. Fuel node — first full system node end-to-end
8. Virtual panel for fuel — first full `/devices/virtual/` → arbitrator → sim path test
9. IOS backend skeleton + WebSocket bridge
10. IOS frontend skeleton — run/pause/reset + arbitration status panel
11. Expand system nodes one by one
12. CIGI bridge
13. micro-ROS bridge (hardware path through arbitrator)
14. nav_sim integration (connect `/sim/world/nav_signals`)
15. QTG engine
16. Debriefing / replay UI

---

## What NOT to Do

- Never call FDM code directly from a systems node — always go through `/sim/fdm/state`
- Never hard-code aircraft parameters in node code — always read from aircraft YAML config
- Never let system nodes subscribe to each other — all coupling via `/sim/fdm/state`, `/sim/failures/active`, or `/sim/world/`
- Never let any sim node subscribe to `/devices/` topics — only the input_arbitrator reads device topics
- Never put IOS logic in the Sim Manager — IOS sends commands, Sim Manager executes them
- Never store sim state in the IOS backend — it is stateless, ROS2 is the source of truth
- Never publish controls from hardware bridge and virtual panels on the same topic — they use separate `/devices/hardware/` and `/devices/virtual/` paths, the arbitrator selects between them
- Never compute atmosphere, signal reception, or terrain in a systems node — subscribe to `/sim/world/` instead

---

## Open Decisions (to resolve as we build)

- [x] Sim clock: use ROS2 native sim time (`/clock` + `use_sim_time`) ✓
- [x] Nav signals and world environment: `/sim/world/` namespace, external services via ROS2 topics ✓
- [x] CGF: scripted entities in scenario files first; live IOS control panel deferred; standalone CGF tool out of scope ✓
- [ ] Virtual panel rendering: web (React), OpenGL native, VR (OpenXR), or multiple simultaneously?
- [ ] ROS2 message type definitions — custom msgs or std_msgs where possible?
- [ ] CIGI library: implement from scratch or use existing open-source (e.g. cigicl)?
- [ ] micro-ROS transport: serial UART or CAN bus per MCU?
- [ ] IOS auth: single-user (no auth) or multi-role (instructor / examiner / admin)?
- [ ] Scenario file format: custom YAML schema or adopt an existing standard?
- [ ] First target aircraft for integration testing: C172 or EC135?
- [ ] Terrain service: separate node or merged into nav_sim project?
- [ ] nav_sim interface: finalise NavSignalTable message type when nav_sim project is ready
