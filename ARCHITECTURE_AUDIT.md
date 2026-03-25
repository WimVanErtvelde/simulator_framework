# Architecture Audit — Simulator Framework

**Date:** 2026-03-25
**Scope:** Read-only review of all nodes, messages, configs, and wiring
**Method:** Automated agent analysis of every .cpp, .hpp, .py, .msg, .srv, .yaml file under src/, ios/, and launch/

---

## Finding Tags

- `[BUG]` — Actually broken or will break
- `[INCONSISTENCY]` — Works but doesn't follow the established pattern
- `[DEAD]` — Code/fields that serve no purpose
- `[MISSING]` — Error handling, documentation, or wiring that should exist but doesn't
- `[DRIFT]` — Documentation says X but code does Y

---

## Section 1 — Configuration Inventory

### Summary Table

| Node | ROS2 Params | Aircraft YAML | Path Derivation | Findings |
|---|---|---|---|---|
| sim_manager | `aircraft_id` (required), `time_scale`, `aircraft_config_dir` | `config.yaml` | 6 relative path candidates | `[INCONSISTENCY]` relative paths only, no ament |
| flight_model_adapter | `aircraft_id`, `fdm_type`, `update_rate_hz`, `jsbsim_root_dir` | `config.yaml` | `ament_index_cpp` | `[INCONSISTENCY]` hardcoded c172 model name |
| input_arbitrator | `hardware_timeout_ms`, `update_rate_hz` | None | N/A | `[INCONSISTENCY]` rate is int, not double |
| atmosphere_node | `update_rate_hz` | None | N/A | Clean |
| cigi_bridge | `ig_address`, `ig_port`, `host_port`, `entity_id`, `publish_rate_hz`, `aircraft_id`, `aircraft_config_path` | `config.yaml` + `cigi_bridge.yaml` | AMENT_PREFIX_PATH walk + relative fallback | `[INCONSISTENCY]` custom YAML parser |
| navaid_sim | `data_source`, `navdb_path`, `terrain_dir`, etc. | None | Paths from launch file params | `[INCONSISTENCY]` all relative paths |
| sim_electrical | `aircraft_id` | `electrical.yaml` | `ament_index_cpp` | `[INCONSISTENCY]` rate hardcoded 50Hz, not a param |
| sim_fuel | `aircraft_id`, `update_rate_hz` | `fuel.yaml` | `ament_index_cpp` | Clean |
| sim_engine_systems | `aircraft_id` | `engine.yaml` | `ament_index_cpp` | `[BUG]` hardcoded switch IDs |
| sim_gear | `aircraft_id` | `gear.yaml` | `ament_index_cpp` | Clean |
| sim_air_data | `aircraft_id` | `air_data.yaml` | `ament_index_cpp` | Clean |
| sim_navigation | `update_rate_hz` | None | N/A | Clean — aircraft-agnostic |
| sim_failures | `aircraft_id` | `failures.yaml` | **Hardcoded relative path** | `[BUG]` only node using relative path |
| sim_hydraulic | None | None | N/A | Stub |
| sim_ice_protection | None | None | N/A | Stub |
| sim_pressurization | None | None | N/A | Stub |
| ios_backend | `use_sim_time` only | `navigation.yaml`, `engine.yaml`, `fuel.yaml`, `failures.yaml` | `ament_index_python` | `[INCONSISTENCY]` hardcoded c172 at startup |

### Detailed Findings

**F1.1 `[BUG]` sim_engine_systems hardcodes switch ID strings**
`engines_node.cpp:296-318` — Switch IDs `"sw_starter_0"`, `"sw_ignition_0"`, `"sw_magnetos"`, `"sel_prop_0"`, `"sel_condition_0"`, `"sel_power_0"` are hardcoded in framework code. These are aircraft-specific panel IDs. Violates "no hardcoded aircraft-specific values in framework code." Should be driven by `engine.yaml`.

**F1.2 `[BUG]` sim_failures uses relative path for YAML**
`failures_node.cpp:153` — `"src/aircraft/" + aircraft_id + "/config/failures.yaml"`. Every other systems node uses `ament_index_cpp::get_package_share_directory()`. This path only works when launched from workspace root.

**F1.3 `[INCONSISTENCY]` JSBSimAdapter hardcodes c172 model name**
`JSBSimAdapter.cpp` — `if (aircraft_id == "c172") { model_name = "c172p"; }`. Aircraft-specific logic in framework code.

**F1.4 `[INCONSISTENCY]` JSBSimAdapter hardcodes default IC (EBBR rwy 25L)**
Default lat/lon/heading hardcoded in `initialize()`. Should come from aircraft `config.yaml`.

**F1.5 `[INCONSISTENCY]` sim_manager uses relative config paths**
All 6 config search paths are workspace-relative strings. Other nodes use `ament_index_cpp` for installed paths.

**F1.6 `[INCONSISTENCY]` Clock rate (50 Hz) not a declared parameter**
`sim_manager` constructor hardcodes 20ms wall timer. CLAUDE.md says "50 Hz default, configurable." The parameter does not exist.

**F1.7 `[INCONSISTENCY]` sim_electrical hardcodes 50 Hz update rate**
Unlike `sim_fuel` which declares `update_rate_hz`, `sim_electrical` hardcodes 20ms.

**F1.8 `[INCONSISTENCY]` input_arbitrator declares rate as int**
`update_rate_hz` is `int`, not `double`. All other nodes use `double`.

**F1.9 `[INCONSISTENCY]` ios_backend hardcodes c172 at startup**
Lines 240-243 pre-load c172 configs before any SimState arrives. If sim starts with ec135, wrong configs are served briefly.

**F1.10 `[INCONSISTENCY]` ios_backend port 8080 not configurable**
Hardcoded in uvicorn.run(). Not a ROS2 parameter or env var.

**F1.11 `[INCONSISTENCY]` DEFAULT_DT_HZ independent of update_rate_hz**
`JSBSimAdapter.cpp` uses `DEFAULT_DT_HZ = 50.0` for fuel drain calculation, independent of the node's `update_rate_hz` parameter.

**F1.12 `[INCONSISTENCY]` cigi_bridge custom YAML parser**
`load_gear_points()` uses hand-rolled line-by-line parser instead of yaml-cpp. Cannot handle YAML anchors, multi-line values, or indentation variations.

**F1.13 `[INCONSISTENCY]` cigi_bridge HOT rate thresholds are magic numbers**
AGL thresholds (10m, 100m) for HOT rate gating are hardcoded. Should be in `cigi_bridge.yaml`.

**F1.14 `[INCONSISTENCY]` Navaid search only supports XP format**
`ios_backend` `_load_navaid_database()` parses only `earth_nav.dat`. Default deployment uses a424/EURAMEC.PC. `/api/navaids/search` silently returns empty results.

---

## Section 2 — Topic Wiring Verification

### Per-Node Wiring

#### sim_manager
| Direction | Topic | Type | QoS |
|---|---|---|---|
| PUB | `/clock` | `rosgraph_msgs/Clock` | best_effort |
| PUB | `/sim/state` | `SimState` | 10 |
| PUB | `/sim/alerts` | `SimAlert` | 10 |
| PUB | `/sim/initial_conditions` | `InitialConditions` | transient_local |
| PUB | `/sim/scenario/event` | `ScenarioEvent` | 10 |
| PUB | `/sim/heartbeat/sim_manager` | `std_msgs/Header` | 10 |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` | 10 |
| SUB | `/sim/command` | `SimCommand` | 10 |
| SUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| SUB | `/sim/terrain/ready` | `std_msgs/Bool` | 10 |

#### flight_model_adapter
| Direction | Topic | Type | QoS |
|---|---|---|---|
| PUB | `/sim/flight_model/state` | `FlightModelState` | LifecyclePub |
| PUB | `/sim/flight_model/capabilities` | `FlightModelCapabilities` | transient_local |
| PUB | `/sim/terrain/source` | `TerrainSource` | 10 |
| PUB | `/sim/terrain/ready` | `std_msgs/Bool` | 10 |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` | 10 |
| SUB | `/sim/state` | `SimState` | 10 |
| SUB | `/sim/initial_conditions` | `InitialConditions` | 10 |
| SUB | `/sim/cigi/hat_responses` | `HatHotResponse` | 10 |
| SUB | `/sim/engines/commands` | `EngineCommands` | 10 |
| SUB | `/sim/failure/flight_model_commands` | `FailureInjection` | 10 |
| SUB | `/sim/controls/flight` | `FlightControls` | 10 |
| SUB | `/sim/controls/engine` | `EngineControls` | 10 |
| SUB | `/sim/writeback/electrical` | `ElectricalState` | on_activate only |
| SUB | `/sim/writeback/fuel` | `FuelState` | on_activate only |

#### input_arbitrator
| Direction | Topic | Type | QoS |
|---|---|---|---|
| PUB | `/sim/controls/flight` | `FlightControls` | LifecyclePub |
| PUB | `/sim/controls/engine` | `EngineControls` | LifecyclePub |
| PUB | `/sim/controls/avionics` | `AvionicsControls` | LifecyclePub |
| PUB | `/sim/controls/panel` | `PanelControls` | LifecyclePub |
| PUB | `/sim/controls/arbitration` | `ArbitrationState` | LifecyclePub |
| PUB | `/sim/alerts` | `SimAlert` | LifecyclePub |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` | 10 |
| SUB | `/devices/hardware/controls/flight` | `RawFlightControls` | 10 |
| SUB | `/devices/virtual/controls/flight` | `RawFlightControls` | 10 |
| SUB | `/devices/instructor/controls/flight` | `RawFlightControls` | 10 |
| SUB | `/devices/hardware/controls/engine` | `RawEngineControls` | 10 |
| SUB | `/devices/virtual/controls/engine` | `RawEngineControls` | 10 |
| SUB | `/devices/instructor/controls/engine` | `RawEngineControls` | 10 |
| SUB | `/devices/hardware/controls/avionics` | `RawAvionicsControls` | 10 |
| SUB | `/devices/virtual/controls/avionics` | `RawAvionicsControls` | 10 |
| SUB | `/devices/instructor/controls/avionics` | `RawAvionicsControls` | 10 |
| SUB | `/devices/hardware/panel` | `PanelControls` | 10 |
| SUB | `/devices/virtual/panel` | `PanelControls` | 10 |
| SUB | `/devices/instructor/panel` | `PanelControls` | 10 |
| SUB | `/devices/hardware/heartbeat` | `DeviceHeartbeat` | 10 |
| SUB | `/sim/state` | `SimState` | 10 |

#### atmosphere_node
| Direction | Topic | Type | QoS |
|---|---|---|---|
| PUB | `/sim/world/atmosphere` | `AtmosphereState` | LifecyclePub |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` | 10 |
| SUB | `/sim/flight_model/state` | `FlightModelState` | 10 |
| SUB | `/sim/world/weather` | `WeatherState` | 10 |

#### cigi_bridge
| Direction | Topic | Type | QoS |
|---|---|---|---|
| PUB | `/sim/cigi/hat_responses` | `HatHotResponse` | LifecyclePub |
| PUB | `/sim/cigi/ig_status` | `std_msgs/UInt8` | 10 |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` | 10 |
| SUB | `/sim/flight_model/state` | `FlightModelState` | on_activate |
| SUB | `/sim/flight_model/capabilities` | `FlightModelCapabilities` | transient_local, on_activate |
| SUB | `/sim/state` | `SimState` | on_activate |

#### sim_electrical
| Direction | Topic | Type | QoS |
|---|---|---|---|
| PUB | `/sim/electrical/state` | `ElectricalState` | LifecyclePub |
| PUB | `/sim/writeback/electrical` | `ElectricalState` | 10 |
| PUB | `/sim/alerts` | `SimAlert` | 10 |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` | 10 |
| SUB | `/sim/state` | `SimState` | 10 |
| SUB | `/sim/controls/panel` | `PanelControls` | 10 |
| SUB | `/sim/flight_model/state` | `FlightModelState` | 10 |
| SUB | `/sim/failures/active` | `FailureList` | 10 |
| SUB | `/sim/failure/electrical_commands` | `FailureInjection` | reliable(10) |
| SUB | `/sim/flight_model/capabilities` | `FlightModelCapabilities` | transient_local |

#### sim_fuel
| Direction | Topic | Type | QoS |
|---|---|---|---|
| PUB | `/sim/fuel/state` | `FuelState` | LifecyclePub |
| PUB | `/sim/writeback/fuel` | `FuelState` | 10 |
| PUB | `/sim/alerts` | `SimAlert` | 10 |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` | 10 |
| SUB | `/sim/flight_model/state` | `FlightModelState` | 10 |
| SUB | `/sim/state` | `SimState` | 10 |
| SUB | `/sim/controls/panel` | `PanelControls` | 10 |
| SUB | `/sim/failures/active` | `FailureList` | 10 |
| SUB | `/sim/initial_conditions` | `InitialConditions` | 10 |
| SUB | `/sim/flight_model/capabilities` | `FlightModelCapabilities` | transient_local |

#### sim_engine_systems
| Direction | Topic | Type | QoS |
|---|---|---|---|
| PUB | `/sim/engines/state` | `EngineState` | LifecyclePub |
| PUB | `/sim/engines/commands` | `EngineCommands` | 10 |
| PUB | `/sim/alerts` | `SimAlert` | 10 |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` | 10 |
| SUB | `/sim/state` | `SimState` | 10 |
| SUB | `/sim/flight_model/state` | `FlightModelState` | 10 |
| SUB | `/sim/controls/panel` | `PanelControls` | 10 |
| SUB | `/sim/controls/engine` | `EngineControls` | 10 |
| SUB | `/sim/failures/active` | `FailureList` | 10 |
| SUB | `/sim/electrical/state` | `ElectricalState` | 10 |
| SUB | `/sim/fuel/state` | `FuelState` | 10 |
| SUB | `/sim/flight_model/capabilities` | `FlightModelCapabilities` | transient_local |

#### sim_gear
| Direction | Topic | Type | QoS |
|---|---|---|---|
| PUB | `/sim/gear/state` | `GearState` | LifecyclePub |
| PUB | `/sim/alerts` | `SimAlert` | 10 |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` | 10 |
| SUB | `/sim/state` | `SimState` | 10 |
| SUB | `/sim/flight_model/state` | `FlightModelState` | 10 |
| SUB | `/sim/controls/flight` | `FlightControls` | 10 |
| SUB | `/sim/failures/active` | `FailureList` | 10 |
| SUB | `/sim/failure/gear_commands` | `FailureInjection` | reliable(10) |
| SUB | `/sim/flight_model/capabilities` | `FlightModelCapabilities` | transient_local |

#### sim_air_data
| Direction | Topic | Type | QoS |
|---|---|---|---|
| PUB | `/sim/air_data/state` | `AirDataState` | LifecyclePub |
| PUB | `/sim/alerts` | `SimAlert` | 10 |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` | 10 |
| SUB | `/sim/state` | `SimState` | 10 |
| SUB | `/sim/flight_model/state` | `FlightModelState` | 10 |
| SUB | `/sim/world/atmosphere` | `AtmosphereState` | 10 |
| SUB | `/sim/world/weather` | `WeatherState` | 10 |
| SUB | `/sim/electrical/state` | `ElectricalState` | 10 |
| SUB | `/sim/controls/panel` | `PanelControls` | 10 |
| SUB | `/sim/failures/active` | `FailureList` | 10 |
| SUB | `/sim/failure/air_data_commands` | `FailureInjection` | reliable(10) |
| SUB | `/sim/flight_model/capabilities` | `FlightModelCapabilities` | transient_local |

#### sim_navigation
| Direction | Topic | Type | QoS |
|---|---|---|---|
| PUB | `/sim/navigation/state` | `NavigationState` | LifecyclePub |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` | 10 |
| SUB | `/sim/flight_model/state` | `FlightModelState` | 10 |
| SUB | `/sim/world/nav_signals` | `NavSignalTable` | 10 |
| SUB | `/sim/controls/avionics` | `AvionicsControls` | 10 |
| SUB | `/sim/failure_state` | `FailureState` | reliable(10) |
| SUB | `/sim/flight_model/capabilities` | `FlightModelCapabilities` | transient_local |

#### sim_failures
| Direction | Topic | Type | QoS |
|---|---|---|---|
| PUB | `/sim/failure_state` | `FailureState` | LifecyclePub |
| PUB | `/sim/failure/flight_model_commands` | `FailureInjection` | LifecyclePub |
| PUB | `/sim/failure/electrical_commands` | `FailureInjection` | LifecyclePub |
| PUB | `/sim/failure/navaid_commands` | `FailureInjection` | LifecyclePub |
| PUB | `/sim/failure/air_data_commands` | `FailureInjection` | LifecyclePub |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` | 10 |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` | 10 |
| SUB | `/ios/failure_command` | `FailureCommand` | 10 |
| SUB | `/sim/flight_model/state` | `FlightModelState` | on_activate |

#### ios_backend
| Direction | Topic | Type |
|---|---|---|
| PUB | `/devices/instructor/panel` | `PanelControls` |
| PUB | `/devices/virtual/panel` | `PanelControls` |
| PUB | `/devices/instructor/controls/avionics` | `RawAvionicsControls` |
| PUB | `/devices/instructor/controls/flight` | `RawFlightControls` |
| PUB | `/devices/instructor/controls/engine` | `RawEngineControls` |
| PUB | `/sim/command` | `SimCommand` |
| PUB | `/sim/initial_conditions` | `InitialConditions` |
| PUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` |
| PUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` |
| PUB | `/ios/failure_command` | `FailureCommand` |
| SUB | `/sim/flight_model/state` | `FlightModelState` |
| SUB | `/sim/fuel/state` | `FuelState` |
| SUB | `/sim/state` | `SimState` |
| SUB | `/sim/navigation/state` | `NavigationState` |
| SUB | `/sim/controls/avionics` | `AvionicsControls` |
| SUB | `/sim/electrical/state` | `ElectricalState` |
| SUB | `/sim/alerts` | `SimAlert` |
| SUB | `/sim/engines/state` | `EngineState` |
| SUB | `/sim/diagnostics/heartbeat` | `std_msgs/String` |
| SUB | `/sim/diagnostics/lifecycle_state` | `std_msgs/String` |
| SUB | `/sim/failure_state` | `FailureState` |
| SUB | `/sim/terrain/source` | `TerrainSource` |
| SUB | `/sim/air_data/state` | `AirDataState` |

### Wiring Findings

**F2.1 `[BUG]` `/sim/failures/active` has no publisher**
CLAUDE.md documents `sim_failures` publishing `FailureList` to `/sim/failures/active`. Five nodes subscribe to it (`sim_electrical`, `sim_fuel`, `sim_engine_systems`, `sim_gear`, `sim_air_data`). **sim_failures never publishes to this topic.** It publishes to `/sim/failure_state` (FailureState) instead, and routes injection via `/sim/failure/<handler>_commands`. The `apply_failure()` path via FailureList is completely dead across all five nodes.

**F2.2 `[BUG]` `/ios/failure_command` violates topic naming convention**
ios_backend publishes to `/ios/failure_command`. CLAUDE.md: "Two roots, no exceptions: `/devices/` and `/sim/`." Should be `/devices/instructor/failure_command`.

**F2.3 `[BUG]` Virtual panel routing sends to instructor publisher**
`ios_backend_node.py:1289` — When frontend sends `topic: '/devices/virtual/panel'`, the backend calls `publish_panel()` (which publishes to `/devices/instructor/panel`, INSTRUCTOR priority) instead of `publish_virtual_panel()`. Cockpit-page panel inputs are silently elevated to INSTRUCTOR priority.

**F2.4 `[DRIFT]` Arbitration state topic name**
CLAUDE.md documents `/sim/controls/arbitration/state`. Code publishes to `/sim/controls/arbitration` (no `/state` suffix).

**F2.5 `[DRIFT]` Topics missing from CLAUDE.md**
- `/sim/terrain/ready` (published by FMA, subscribed by sim_manager)
- `/sim/terrain/source` (published by FMA, subscribed by ios_backend)
- `/sim/writeback/electrical` (published by sim_electrical, subscribed by FMA)
- `/sim/writeback/fuel` (published by sim_fuel, subscribed by FMA)
- `/sim/failure_state` (published by sim_failures, subscribed by ios_backend + sim_navigation)
- `/sim/failure/<handler>_commands` topics (published by sim_failures)
- `/ios/failure_command` (published by ios_backend, subscribed by sim_failures)
- `/sim/engines/commands` (published by sim_engine_systems, subscribed by FMA)

**F2.6 `[INCONSISTENCY]` ios_backend publishes to `/sim/command`**
IOS architecture is "purely a control surface" that publishes to `/devices/instructor/`. But `SimCommand` has no arbitration layer, so ios_backend publishes directly to `/sim/command`. This is an architectural grey area that should be documented as an acknowledged exception.

**F2.7 `[DEAD]` ios_backend `_ic_pub` never used**
`InitialConditions` publisher created but `_ic_pub.publish()` is never called anywhere. Also a `/sim/` namespace violation.

**F2.8 `[DEAD]` `/sim/failures/active` subscriber on `sim_navigation`**
sim_navigation subscribes to `/sim/failure_state` (which works), but also stores `latest_caps_` from `/sim/flight_model/capabilities` which is never used.

**F2.9 `[INCONSISTENCY]` sim_engine_systems cross-subscribes to systems nodes**
Subscribes to `/sim/electrical/state` and `/sim/fuel/state`. Same for `sim_air_data` subscribing to `/sim/electrical/state`. CLAUDE.md principle 4 says "systems nodes never talk to each other directly." The node descriptions acknowledge the exception but the rule is not amended.

**F2.10 `[DEAD]` `ArbitrationState` has no subscribers**
Published by input_arbitrator, but no node or ios_backend subscribes to it.

**F2.11 `[DEAD]` `GearState` has no subscribers**
Published by sim_gear, but no node or ios_backend subscribes to it.

**F2.12 `[DEAD]` `ScenarioEvent` has no subscribers**
Published by sim_manager, but nothing subscribes.

**F2.13 `[MISSING]` `/sim/failure/navaid_commands` has no subscriber**
Published by sim_failures, but `navaid_sim` does not subscribe to it. Navaid failures cannot be injected.

---

## Section 3 — Lifecycle Compliance

### Summary Table

| Node | try/catch in on_configure | SimAlert on failure | on_deactivate cancels timers | on_cleanup resets all | HB/LC are rclcpp::Publisher | Auto-activate 100ms | Respects sim state |
|---|---|---|---|---|---|---|---|
| sim_manager | Yes (not lifecycle node) | N/A | N/A | N/A | Yes | N/A | N/A |
| flight_model_adapter | Yes | **No** | Yes | Yes | Yes | Yes | Yes |
| input_arbitrator | N/A (no YAML) | N/A | Yes | Yes | Yes | Yes* | Yes |
| atmosphere_node | N/A (no YAML) | N/A | Yes | Yes | Yes | Yes* | Partial** |
| cigi_bridge | Yes | **No** | Yes | Yes | Yes | Yes* | Yes |
| sim_electrical | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| sim_fuel | Yes | Yes | Yes | Yes | Yes | Yes | **Missing reset** |
| sim_engine_systems | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| sim_gear | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| sim_air_data | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| sim_navigation | N/A (no YAML) | N/A | Yes | Yes | Yes | Yes | **No /sim/state sub** |
| sim_failures | Yes | Yes | **In on_activate** | **Partial** | Yes | Yes | Partial |
| Stubs (3) | N/A | N/A | Yes | Yes | Yes | Yes | N/A |

\* Does not check configure result before calling activate
\** Does not subscribe to `/sim/state`

### Detailed Findings

**F3.1 `[MISSING]` flight_model_adapter does not publish SimAlert on configure failure**
`on_configure` returns `FAILURE` if FDM init fails, but never publishes a SimAlert. The `alert_pub_` exists but isn't used on the failure path.

**F3.2 `[MISSING]` cigi_bridge does not publish SimAlert on configure failure**
Same issue — socket open failure returns `FAILURE` but no SimAlert is published.

**F3.3 `[INCONSISTENCY]` LifecyclePublisher on_deactivate() not called uniformly**
`sim_fuel` and `sim_navigation` correctly call `state_pub_->on_deactivate()`. `sim_electrical`, `sim_engine_systems`, `sim_gear`, and `sim_air_data` do not. The ROS2 lifecycle contract requires this.

**F3.4 `[INCONSISTENCY]` sim_failures creates publishers in on_activate**
All other nodes create publishers and subscriptions in `on_configure`. `sim_failures` creates them in `on_activate` and tears them down in `on_deactivate`. Non-standard.

**F3.5 `[INCONSISTENCY]` sim_failures on_cleanup does not reset alert_pub_**
A RELOAD cycle (cleanup -> configure) would recreate the publisher while the old one still exists.

**F3.6 `[MISSING]` sim_fuel never calls model_->reset() on RESETTING**
`sim_electrical`, `sim_engine_systems`, `sim_gear`, and `sim_air_data` all call `model_->reset()` when RESETTING. `sim_fuel` does not. Fuel quantities will not be restored on sim reset.

**F3.7 `[BUG]` sim_navigation DME HOLD state not reset on RESETTING**
Does not subscribe to `/sim/state`. `dme_hold_valid_`, `dme_hold_distance_nm_`, `dme_hold_gs_kt_` persist across a sim reset. Stale DME HOLD values remain after reset.

**F3.8 `[INCONSISTENCY]` sim_electrical runs solver in INIT/READY states**
Update logic treats `STATE_INIT` and `STATE_READY` as running (calls `model_->update(dt_sec)`). CLAUDE.md says gating is RUNNING/FROZEN/RESETTING only.

**F3.9 `[INCONSISTENCY]` Multiple nodes don't check configure result before activate**
`input_arbitrator`, `atmosphere_node`, and `cigi_bridge` all call `trigger_transition(ACTIVATE)` without verifying configure succeeded. `flight_model_adapter` correctly checks.

**F3.10 `[INCONSISTENCY]` input_arbitrator pub_alert_ is LifecyclePublisher**
Alerts published from subscription callbacks (which fire in any lifecycle state) will be silently dropped when the node is inactive.

**F3.11 `[INCONSISTENCY]` cigi_bridge creates all subscriptions in on_activate**
Every other node creates subscriptions in `on_configure`. cigi_bridge creates them in `on_activate`.

**F3.12 `[MISSING]` sim_navigation has no alert publisher**
Every other non-stub node has `alert_pub_`. sim_navigation cannot surface errors to IOS.

**F3.13 `[MISSING]` IFuelModel interface missing reset() method**
All other model interfaces (`IElectricalModel`, `IGearModel`, `IEnginesModel`, `IAirDataModel`) have `reset()`. `IFuelModel` does not. Related to F3.6.

---

## Section 4 — Error Handling Patterns

### Critical Bugs

**F4.1 `[BUG]` JSBSimAdapter apply_failure — gear clear sets pos-norm to -1.0**
`JSBSimAdapter.cpp` — `set_gear_unable_to_extend` clear path sets `gear/unit[idx]/pos-norm` to `-1.0` instead of `1.0`. A -1.0 normalized gear position is invalid. Likely copy-paste error.

**F4.2 `[BUG]` cigi_bridge inet_aton() return not checked**
`cigi_host_node.cpp` — `inet_aton()` returns 0 on failure but return value is not checked. Invalid `ig_address` silently sends CIGI packets to 0.0.0.0. Should use `inet_pton()` with error checking.

**F4.3 `[BUG]` FailureList severity array bounds access in 3 nodes**
`sim_electrical`, `sim_gear`, `sim_air_data` — loop iterates `msg->failure_ids.size()` but accesses `msg->severity[i]` without checking severity array length. If severity is shorter, this is undefined behaviour. (Currently harmless because the topic is never published — see F2.1 — but will become live when fixed.)

**F4.4 `[BUG]` sim_air_data no null check on model_ in update timer**
`air_data_node.cpp:211` — update timer calls `model_->update()` without checking `model_` is not null. `sim_gear` and `sim_engine_systems` have this guard; `sim_air_data` does not.

### Missing Error Handling

**F4.5 `[MISSING]` JSBSimAdapter uses std::cerr, not RCLCPP_ERROR**
All JSBSimAdapter error logging goes to `std::cerr`, bypassing ROS2 logging. Errors won't appear in ros2 log files. The adapter has no access to an rclcpp logger.

**F4.6 `[MISSING]` cigi_bridge sendto() return not checked**
UDP `sendto()` silently fails on unreachable host. No log indication that CIGI packets aren't being delivered.

**F4.7 `[MISSING]` ios_backend ROS2 callbacks unguarded**
All `_on_*` callbacks have no try/except. A bad ROS2 message could raise an exception that propagates into the `spin_once()` thread, which has a bare `except Exception: pass` (line 1179). This would silently kill all subscription callbacks.

**F4.8 `[MISSING]` ios_backend JSON decode errors silently swallowed**
WebSocket receiver catches `json.JSONDecodeError` with `pass` (line 1336). No log, no error response.

**F4.9 `[MISSING]` cigi_bridge load_gear_points() silently swallows parse errors**
Custom YAML parser has `try/catch(...)` around each `std::stod` call. Individual field parse errors are silently ignored.

**F4.10 `[MISSING]` atmosphere_node density ignores OAT deviation**
Density computed from ISA temperature, not actual OAT. `density_altitude` will be wrong in non-ISA conditions.

### Fragility

**F4.11 `[INCONSISTENCY]` sim_manager reload_client_ overwrite race**
`reload_client_` is overwritten on each `reload_node()` call. If called twice rapidly, the first async chain may reference a destructed client. Lambdas capture by value but the timing is fragile.

**F4.12 `[INCONSISTENCY]` sim_electrical hardcodes 75% N2 seed**
`model_->set_engine_n2({75.0})` at line 197. Seeds electrical solver before real engine data arrives. Value is not in YAML or configurable.

---

## Section 5 — Dead Code and Stale Artifacts

### Stale Root-Level Files

| File | Status | Notes |
|---|---|---|
| `patch_cigi.py` | `[STALE]` | One-shot migration script, already applied |
| `patch_xplane.py` | `[STALE]` | One-shot migration script, already applied |
| `patchios2.py` | `[STALE]` | One-shot migration, uses old `latitude_rad` keys — would regress if re-run |
| `reposition_refactor.tar.gz` | `[STALE]` | Archive from completed refactor |
| `reposition_refactor.tar.gz:Zone.Identifier` | `[STALE]` | Windows ADS metadata |

### Stale Documentation

| File | Status | Notes |
|---|---|---|
| `docs/reposition_design.md` | `[STALE]` | Describes `STATE_REPOSITIONING` which was removed. Contradicts implementation |
| `docs/specs/2026-03-22-repositioning-pipeline-design.md` | `[STALE]` | Proposes `STATE_REPOSITIONING = 6`, removed from SimState.msg |
| `docs/plans/2026-03-22-repositioning-pipeline.md` | `[STALE]` | Task plan with unchecked boxes for reversed tasks |

### Dead Message Types

| Message | Tag | Notes |
|---|---|---|
| `NavState.msg` | `[DEAD]` | Predecessor to `NavigationState.msg`. No code references it |
| `ControlsState.msg` | `[DEAD]` | Contains only `bool placeholder`. Never used |
| `HydraulicState.msg` | `[DEAD]` | Stub for unimplemented hydraulic node |
| `CigiPacket.msg` | `[STALE]` | Documented as "planned — recording/debug". Never instantiated |
| `HotRequest.msg` | `[STALE]` | Raw CIGI encoding supersedes it. Never published or subscribed |

### Dead Code in Source

| Location | Tag | Notes |
|---|---|---|
| `cigi_host_node.cpp` caps_sub_ callback | `[DEAD]` | Subscription with empty callback body (comment only) |
| `cigi_ig_interface/RepositionBase.cpp+.h` | `[DEAD]` | Legacy UDP repositioning, not instantiated in ROS2 code |
| `cigi_ig_interface/` directory | `[STALE]` | CCL-dependent vendored library, conditionally excluded (CCL not installed) |
| `JSBSimAdapter.cpp:469` cas_ms | `[DEAD]` | Field set but never read by any consumer |
| `JSBSimAdapter.cpp:489-490` baro_altitude_m, radar_altitude_m | `[DEAD]` | Fields set but never read by any consumer |
| `atmosphere_node.cpp` flight_model_received_, weather_received_ | `[DEAD]` | Flags set but never checked |
| `sim_navigation` latest_caps_ | `[DEAD]` | Capabilities received, stored, never queried |
| `src/aircraft/c172/config/engines.yaml` | `[DEAD]` | No node loads this file (sim_engine_systems uses `engine.yaml`) |
| `src/aircraft/ec135/config/engines.yaml` | `[DEAD]` | Same |
| `INavigationModel` interface | `[DEAD]` | Exists in sim_interfaces but never used (no pluginlib for navigation) |
| `ios_backend _ic_pub` | `[DEAD]` | Publisher declared, never called |
| `sim_manager /sim/heartbeat/sim_manager` | `[DEAD]` | Header-type heartbeat on non-standard topic. Not consumed by any node |
| `x-plane_plugins/RepositionAccessor.h:32` | `[BUG]` | `GetAlt()` returns `m_lat()` instead of `m_alt()` (copy-paste bug) |

### TODO / FIXME Comments

| Location | Text |
|---|---|
| `JSBSimAdapter.cpp:432` | `// TODO: apply magnetic declination` — magnetic_heading_rad = true heading |
| `JSBSimAdapter.cpp:503` | `// TODO: compute from wind components` — wind_direction_rad always 0.0 |
| `ios_backend/setup.py:16-19` | `maintainer='TODO', maintainer_email='todo@todo.com', license='TODO'` |
| `qtg/engine/setup.py:16-19` | Same boilerplate placeholders |

---

## Section 6 — Message Field Usage Coverage

### Messages with Significant Dead Fields

#### FlightModelState — Massive field set, many unused

**Published by:** flight_model_adapter (JSBSimAdapter)
**Consumed by:** sim_manager, cigi_bridge, atmosphere_node, sim_electrical, sim_fuel, sim_engine_systems, sim_gear, sim_air_data, sim_navigation, navaid_sim, ios_backend

**Fields never consumed by any subscriber:**
- `flight_model_name`, `aircraft_id`, `has_multi_engine`, `has_retractable_gear`
- `cap_models_fuel_*` (3 capability fields)
- `is_frozen`
- `ecef_x/y/z_m` (ECEF position)
- `q_w/x/y/z` (quaternion attitude)
- `vel_north/east/down_ms` (NED velocity)
- `vel_u/v/w_ms` (body velocity)
- `vel_ecef_*_ms` (ECEF velocity)
- `roll/pitch/yaw_rate_rads`
- `accel_*_ms2` (body accelerations)
- `load_factor_*` (3 load factors)
- `tas_ms`, `cas_ms`, `eas_ms` (speed variants — air_data uses `ias_kts` instead)
- `mach_number`
- `alpha_rad`, `beta_rad`, `alpha_dot_rads`, `beta_dot_rads`
- `baro_altitude_m`, `radar_altitude_m`
- `dynamic_pressure_pa`, `static_pressure_pa`, `air_density_kgm3`, `temperature_k`
- `wind_direction_rad`, `wind_vertical_ms`
- `throttle_pct[4]`, `n2_rpm[4]`, `power_turbine_rpm/pct[4]`, `torque_pct[4]`, `oil_pressure_pa[4]`, `oil_temperature_k[4]`
- `total_mass_kg`, `crashed`, `flight_state`
- Entire autopilot section (11 fields)
- Entire control surface section (9 fields)
- Entire rotor section (12 fields)
- `sim_clock`

Many of these are populated by JSBSimAdapter for future use (QTG, replay, virtual cockpit instruments). They are not currently dead by design — they are "publish ahead of consumption." However, `cas_ms`, `baro_altitude_m`, `radar_altitude_m` are stub values that could mislead future consumers.

#### NavigationState — Future-proofed fields all zero

**Always-zero sections:** GPS2 (entire set), ADF2 (entire set), TACAN (entire set), `transponder_ident_active`

#### ElectricalState — CB arrays populated but unread

`cb_names`, `cb_closed`, `cb_tripped` arrays are populated by sim_electrical but no subscriber reads them. The IOS frontend reads individual load_powered states instead.

#### EngineState — Most fields never consumed

Engine arrays for torque_nm, shp_kw, generator_online, prop_rpm, main_rotor_rpm, tail_rotor_rpm, FADEC mode fields, EPR, beta_deg, feather_state, reverse_active, vibration_level, boost_pressure_inhg, intercooler_temp_degc are all populated but never consumed.

#### AirDataState — Only system[0] ever read

Supports up to 3 pitot-static systems but only index [0] is consumed by ios_backend/frontend. `system_count`, `system_names[3]`, and `alternate_static_active[3]` are populated but unread.

#### WeatherState — Published by sim_manager, partially consumed

`wind_direction_deg`, `wind_speed_kts`, `wind_gust_kts`, `turbulence_intensity`, `visibility_m`, `cloud_base_ft`, `cloud_coverage` are published but only `turbulence_intensity`, `visible_moisture`, `oat_deviation_k`, and `qnh_hpa` are consumed.

#### AtmosphereState — Several fields unread

`temperature_k` (ISA temp), `speed_of_sound_ms`, `density_altitude_m`, `pressure_altitude_m` are published but never consumed. Only `oat_k`, `pressure_pa`, `density_kgm3`, `qnh_pa` are read.

### Messages with No Subscribers at All

| Message | Published By | Notes |
|---|---|---|
| `ArbitrationState` | input_arbitrator | No node or ios_backend subscribes |
| `GearState` | sim_gear | No node or ios_backend subscribes |
| `ScenarioEvent` | sim_manager | No subscriber exists |
| `EngineCommands` | sim_engine_systems | FMA subscribes but the callback is a no-op stub |

### Messages Never Published or Subscribed

| Message | Notes |
|---|---|
| `NavState` | Legacy predecessor to NavigationState |
| `ControlsState` | Placeholder with single bool |
| `HydraulicState` | Stub node, not connected |
| `CigiPacket` | Planned, never implemented |
| `HotRequest` | Raw encoding approach superseded it |
| `TerrainState` | No publisher exists yet |

### Service Findings

**F6.1 `[DEAD]` GetTerrainElevation.srv — server exists, no client**
Provided by navaid_sim but no node or ios_backend calls it.

### Message Design Issues

**F6.2 `[DRIFT]` RawFlightControls missing fields present in FlightControls**
`FlightControls` has `flaps`, `gear_down`, `spoilers`, `speed_brake`, `parking_brake`, `rotor_brake` that have no equivalent in `RawFlightControls`. These fields cannot be commanded by any device source — they can only originate from the input_arbitrator's internal state.

**F6.3 `[DRIFT]` FlightModelState magnetic_heading_rad is true heading**
`JSBSimAdapter.cpp:432` sets `magnetic_heading_rad = psi` (true heading). TODO comment acknowledges this.

**F6.4 `[DRIFT]` FlightModelState wind_direction_rad always zero**
`JSBSimAdapter.cpp:503` hardcodes `wind_direction_rad = 0.0`. TODO comment.

**F6.5 `[DRIFT]` AirDataState not received by frontend**
ios_backend sends `air_data_state` over WebSocket but `useSimStore.js` has no `case 'air_data_state'` handler. Data is transmitted but silently dropped.

---

## Section 7 — Missing EC135 Configuration

| Config File | C172 | EC135 | Impact |
|---|---|---|---|
| `config.yaml` | Present | Present (missing `gear_ground_height_m`, `gear_points`) | CIGI HOT falls back to defaults |
| `electrical.yaml` | Present | Present | OK |
| `fuel.yaml` | Present | Present | OK |
| `engine.yaml` | Present | Present | OK |
| `gear.yaml` | Present | Present (missing `index:` per leg) | May fail or silently misparse |
| `air_data.yaml` | Present | **MISSING** | `[BUG]` sim_air_data will FAIL on configure |
| `failures.yaml` | Present | Present | OK |
| `navigation.yaml` | Present | Present | OK |

The launch file starts `sim_air_data` unconditionally. EC135's `required_nodes` does not list `sim_air_data`, but the node will still start and fail. `[INCONSISTENCY]` — launch file does not gate nodes by aircraft type.

---

## Consolidated Finding Index

### Bugs (must fix)

| ID | Location | Description |
|---|---|---|
| F2.1 | sim_failures | `/sim/failures/active` never published — 5 nodes subscribe, all receive nothing. apply_failure() path completely dead |
| F2.3 | ios_backend:1289 | Virtual panel message routed to instructor publisher — elevates VIRTUAL to INSTRUCTOR priority |
| F1.1 | engines_node.cpp:296-318 | Hardcoded switch ID strings in framework code |
| F1.2 | failures_node.cpp:153 | Hardcoded relative YAML path (only node not using ament) |
| F4.1 | JSBSimAdapter.cpp | apply_failure gear clear sets pos-norm to -1.0 (should be 1.0) |
| F4.2 | cigi_host_node.cpp | inet_aton() return not checked — invalid address silently sends to 0.0.0.0 |
| F4.3 | electrical/gear/air_data | FailureList severity array bounds access without length check (UB) |
| F4.4 | air_data_node.cpp | No null check on model_ in update timer |
| F3.7 | navigation_node.cpp | DME HOLD state not reset on RESETTING |
| EC135 | aircraft/ec135/config/ | Missing air_data.yaml — sim_air_data will FAIL on configure |

### Missing

| ID | Location | Description |
|---|---|---|
| F3.1 | flight_model_adapter | No SimAlert published on configure failure |
| F3.2 | cigi_bridge | No SimAlert published on configure failure |
| F3.6 | sim_fuel | Never calls model_->reset() on RESETTING |
| F3.12 | sim_navigation | No alert publisher |
| F3.13 | IFuelModel interface | Missing reset() method |
| F4.5 | JSBSimAdapter | All error logging via std::cerr, not RCLCPP_ERROR |
| F4.7 | ios_backend | ROS2 callbacks unguarded — bad message kills spin thread |
| F4.10 | atmosphere_node | Density ignores OAT deviation |
| F2.13 | sim_failures/navaid_sim | `/sim/failure/navaid_commands` has no subscriber |
| F6.5 | useSimStore.js | No handler for `air_data_state` WebSocket messages |

### Inconsistencies

| ID | Location | Description |
|---|---|---|
| F1.3 | JSBSimAdapter | Hardcoded c172 -> c172p model name mapping |
| F1.4 | JSBSimAdapter | Hardcoded default IC (EBBR) |
| F1.5 | sim_manager | Relative config paths (should use ament) |
| F1.6 | sim_manager | Clock rate not a parameter despite docs saying "configurable" |
| F2.2 | ios_backend | `/ios/failure_command` violates topic naming convention |
| F2.6 | ios_backend | Publishes to `/sim/command` (grey area) |
| F2.9 | sim_engine_systems/sim_air_data | Cross-subscribe to systems node topics |
| F3.3 | electrical/engines/gear/air_data | LifecyclePublisher on_deactivate() not called |
| F3.4 | sim_failures | Creates publishers in on_activate instead of on_configure |
| F3.8 | sim_electrical | Runs solver in INIT/READY states |
| F3.9 | input_arbitrator/atmosphere/cigi | Don't check configure result before activate |

### Documentation Drift

| ID | Location | Description |
|---|---|---|
| F2.4 | input_arbitrator | Topic is `/sim/controls/arbitration`, docs say `/sim/controls/arbitration/state` |
| F2.5 | Multiple | 8+ topics exist in code but missing from CLAUDE.md topic tables |
| F6.2 | RawFlightControls | Missing fields that FlightControls has |
| F6.3 | FlightModelState | magnetic_heading_rad is actually true heading |
| F6.4 | FlightModelState | wind_direction_rad always zero |

### Dead Code

| ID | Location | Description |
|---|---|---|
| F2.7 | ios_backend | _ic_pub publisher never used |
| F2.10 | ArbitrationState | Published, zero subscribers |
| F2.11 | GearState | Published, zero subscribers |
| F2.12 | ScenarioEvent | Published, zero subscribers |
| Msgs | NavState, ControlsState, HydraulicState, CigiPacket, HotRequest | Never published or subscribed |
| Files | patch_*.py, reposition_refactor.tar.gz, docs/reposition_design.md | Stale artifacts |
| Code | caps_sub_ (cigi), RepositionBase, engines.yaml (both aircraft) | Unused code/config |
