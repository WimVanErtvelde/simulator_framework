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
4. **Systems nodes never talk to each other directly** — they subscribe to `/fdm/state` and publish their own output. Coupling goes through the FDM adapter or the Sim Manager only.
5. **Hardware/software parity** — virtual panel nodes and real micro-ROS hardware nodes publish/subscribe on identical topics. The rest of the sim cannot tell the difference.
6. **Aircraft config drives everything** — which nodes load, which FDM adapter runs, which instrument panels show, all driven by YAML config per aircraft type.
7. **IOS is purely a control surface** — it injects commands via ROS2 topics. It has no privileged access to sim internals.

---

## Tech Stack

| Layer | Technology |
|---|---|
| Middleware | ROS2 Jazzy (LTS) |
| Systems nodes | C++ (real-time, deterministic) |
| FDM adapter | C++ with abstract plugin interface |
| IOS backend | Python / FastAPI + rclpy |
| IOS frontend | React + Zustand + WebSocket |
| Virtual panels | React (same frontend codebase, separate views) |
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
│   └── cigi_bridge/                 ← ROS2 node, CIGI host implementation
├── systems/
│   ├── electrical/                  ← ROS2 node
│   ├── fuel/                        ← ROS2 node
│   ├── hydraulic/                   ← ROS2 node
│   ├── navigation/                  ← ROS2 node
│   ├── engines/                     ← ROS2 node
│   ├── failures/                    ← ROS2 node, broadcasts active failures
│   └── ...                          ← extend per aircraft requirements
├── hardware/
│   ├── microros_bridge/             ← ROS2 node, micro-ROS agent interface
│   └── virtual_panels/              ← React UI simulating physical instrument panels
├── ios/
│   ├── backend/                     ← FastAPI + rclpy ROS2 bridge
│   └── frontend/                    ← React IOS web app
├── qtg/
│   ├── engine/                      ← automated test runner + data extractor
│   ├── maneuvers/                   ← test maneuver scripts (YAML)
│   └── reports/                     ← generated QTG PDF output
├── aircraft/
│   ├── c172/                        ← config YAML, FDM reference data, panel layout
│   └── ec135/                       ← rotary-wing example
└── tools/
    └── scenario_editor/             ← optional: GUI scenario builder
```

---

## Sim Manager

**The heartbeat of the framework.**

- Owns and publishes `/sim/clock` at fixed rate (50 Hz default, configurable)
- Manages sim state machine:

```
INIT → READY → RUNNING ↔ PAUSED → REPLAY
                  ↓
               FROZEN (position / fuel / attitude flags)
```

- Loads aircraft config YAML on startup
- Spawns/tears down system nodes based on aircraft config
- Exposes a ROS2 service interface for IOS commands
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

**Publishes:** `/fdm/state` (position, attitude, velocities, accelerations, aero forces, weight on wheels)

---

## ROS2 Topic Conventions

All topics use snake_case. Namespaced by subsystem.

| Topic | Type | Publisher | Notes |
|---|---|---|---|
| `/sim/clock` | SimClock | sim_manager | Drives all nodes |
| `/sim/state` | SimState | sim_manager | RUNNING, PAUSED, etc. |
| `/fdm/state` | FdmState | flight_model_adapter | Core truth data |
| `/controls/flight` | FlightControls | hardware_bridge or virtual_panel | Yoke, pedals, collective |
| `/controls/engine` | EngineControls | hardware_bridge or virtual_panel | Throttle, mixture, etc. |
| `/electrical/buses` | ElectricalState | electrical_node | |
| `/fuel/state` | FuelState | fuel_node | Quantities, flow, CG |
| `/hydraulic/state` | HydraulicState | hydraulic_node | |
| `/nav/state` | NavState | navigation_node | VOR, ILS, GPS, ADF |
| `/engine/state` | EngineState | engines_node | N1/N2, EGT, torque, etc. |
| `/failures/active` | FailureList | failures_node | Broadcast to all nodes |
| `/ios/command` | IosCommand | ios_backend | IOS → sim |
| `/weather/state` | WeatherState | sim_manager | Wind, vis, clouds |
| `/cigi/out` | CigiPacket | cigi_bridge | Host → IG |
| `/cigi/in` | CigiPacket | cigi_bridge | IG → Host |

---

## Systems Nodes

Each system node follows this pattern:
- Subscribes to `/sim/clock`, `/fdm/state`, `/failures/active`, and its own relevant controls topic
- Runs its update logic on each `/sim/clock` tick
- Publishes its state topic
- Reads its configuration from the aircraft YAML on init
- Respects freeze flags from Sim Manager

**Nodes planned:**
- `electrical_node` — DC/AC buses, battery, alternators, circuit breakers
- `fuel_node` — tanks, feed, transfer, CG, freeze support
- `hydraulic_node` — system A/B, accumulator, emergency
- `navigation_node` — VOR, ILS, GPS, ADF, transponder
- `engines_node` — N1/N2, EGT, torque, fuel flow, start sequence
- `failures_node` — central failure injector, reads IOS commands, broadcasts active failures
- `ice_protection_node` — de-ice, anti-ice, pitot heat
- `pressurization_node` — (for applicable aircraft)
- `gear_node` — retract/extend, weight on wheels, warnings

---

## CIGI Bridge

- Implements CIGI 3.3 / 4.0 host side
- Subscribes to `/fdm/state`, `/weather/state`, `/scenario/entities`
- Sends to IG: entity position/attitude, atmosphere, time-of-day, special effects
- Receives from IG: line-of-sight responses, collision events
- IG identity is completely hidden from the rest of the sim

---

## Hardware Bridge & Virtual Panels

### Design rule
Virtual panel nodes and real micro-ROS hardware nodes **must publish and subscribe on identical topics**.
The rest of the sim must never know which is active.

### micro-ROS path
```
MCU (ESP32/STM32)
  └── micro-ROS firmware
        └── UDP
              └── micro-ROS agent (ROS2 node)
                    └── ROS2 topics  (same as virtual panels)
```

### Virtual panels path
```
React virtual panel UI
  └── WebSocket
        └── IOS backend (rclpy bridge)
              └── ROS2 topics  (same as hardware)
```

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

## Build Order (implementation sequence)

1. `CLAUDE.md` ← you are here
2. ROS2 package scaffold (all packages, no logic yet)
3. Sim Manager — clock + state machine only
4. Flight Model Adapter — interface + JSBSim implementation
5. Fuel node — first full system node end-to-end
6. Virtual panel for fuel — first hardware/software parity test
7. IOS backend skeleton + WebSocket bridge
8. IOS frontend skeleton — run/pause/reset only
9. Expand system nodes one by one
10. CIGI bridge
11. micro-ROS bridge
12. QTG engine
13. Debriefing / replay UI

---

## What NOT to Do

- Never call FDM code directly from a systems node — go through `/fdm/state` topic
- Never hard-code aircraft parameters in node code — always read from aircraft YAML config
- Never let system nodes subscribe to each other — all coupling via `/fdm/state` or failure topic
- Never put IOS logic in the Sim Manager — IOS sends commands, Sim Manager executes them
- Never store sim state in the IOS backend — it is stateless, ROS2 is the source of truth
- Never run virtual panels and hardware bridge simultaneously on the same topic namespace

---

## Open Decisions (to resolve as we build)

- [ ] ROS2 message type definitions — custom msgs or std_msgs where possible?
- [ ] Sim clock: use ROS2 sim time or custom `/sim/clock` topic?
- [ ] CIGI library: implement from scratch or use existing open-source (e.g. cigicl)?
- [ ] micro-ROS transport: serial UART or CAN bus per MCU?
- [ ] IOS auth: single-user (no auth) or multi-role (instructor / examiner / admin)?
- [ ] Scenario file format: custom YAML schema or adopt an existing standard?
- [ ] First target aircraft for integration testing: C172 or EC135?
