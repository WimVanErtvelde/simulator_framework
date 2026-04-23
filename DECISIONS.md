# DECISIONS.md
#
# Two sections:
#   1. CURRENT STATE — editable living summary (~50 lines), updated periodically
#   2. CHANGE LOG    — append-only forever, never edit past entries
#
# Claude Code: read CURRENT STATE first for context. Append to CHANGE LOG for new decisions.
# Claude Chat: paste this file at the start of each design session.
# ─────────────────────────────────────────────────────────────────────────────

## CURRENT STATE
<!-- Last updated: 2026-04-23 — This section is editable -->

### Architecture
- **Middleware:** ROS2 Jazzy (LTS), all nodes use sim time (`/clock` + `use_sim_time`)
- **Sim Manager** owns `/clock` (50 Hz), state machine (INIT→READY→RUNNING↔FROZEN→RESETTING→READY). Heartbeat monitoring (2s timeout). CMD_REPOSITION=11 triggers FROZEN + `reposition_active` flag + IC broadcast + terrain wait + return to previous state. No separate REPOSITIONING sim state.
- **Input Arbitrator** sole subscriber to `/aircraft/devices/*`. Priority: INSTRUCTOR > HARDWARE > VIRTUAL > FROZEN. 4 channels: flight, engine, avionics, panel. Hardware timeout 500ms → auto-fallback.
- **Flight Model Adapter** abstract C++ interface (`IFlightModelAdapter`). Implementations: JSBSimAdapter, XPlaneUDPAdapter, HelisimUDPAdapter. Publishes `/aircraft/fdm/state` + `/aircraft/fdm/capabilities` (transient_local). CapabilityMode tri-state: FDM_NATIVE / EXTERNAL_COUPLED / EXTERNAL_DECOUPLED. Subscribes to writeback topics for coupled subsystems. Terrain refinement: runway DB altitude → CIGI HOT via `refine_terrain_altitude` (RunIC with cockpit save/restore). `pending_ic_` gates stepping during terrain wait (30s timeout).
- **Systems nodes** (C++) subscribe to `/aircraft/fdm/state` + `/sim/failures/route/<handler>` + `/world/*`, publish own `/aircraft/<s>/state`. Aircraft-specific logic via pluginlib plugins. Cross-subscriptions allowed only for documented physical coupling (engines→electrical, engines→fuel, air_data→electrical).
- **Electrical solver** — GraphSolver (`graph_solver.hpp/cpp`, namespace `elec_graph`). Graph-based topology: nodes (sources, buses, junctions, loads) + connections (wire, switch, contactor, relay, circuit_breaker). V2 YAML format. BFS power propagation with multi-pass relay coil updates. CB trips only via failure effects or IOS commands. YAML validation on load (duplicate IDs, dangling from/to/coil_bus, orphaned loads, CAS target checks). Old `ElectricalSolver` (`elec_sys`) deleted. EC135 electrical plugin stubbed until v2 YAML written.
- **Failure effects** — property overrides on graph elements via generic actions (force, jam, disable, multiply). `failures.yaml` references graph element IDs directly (e.g. `alternator`, `cb_fuel_pump`). No per-failure C++ code — adding new electrical failures is pure YAML authoring. Solver applies active overrides before computing each element.
- **CIGI bridge** — CCL-based via shared `cigi_session` library (Boeing CCL 3.3.3). Host node uses `HostSession::BeginFrame/Append*/FinishFrame`; plugin uses `IgSession` with processor interfaces for each inbound packet class. Byte order: CCL-native (sender writes host order + Byte Swap Magic 0x8000 in IG Control/SOF; recipient auto-swaps). Entity Control + IG Control at 60 Hz. HOT terrain requests rate-gated by AGL (50Hz <10m, 10Hz 10-100m, off above), sent on a second IG-Control-led datagram (CIGI 3.3 §4.1.1 requires IG Control as first packet). Repositioning handshake: IG Mode Reset for one frame on `reposition_active` rising edge, clear HAT tracker, then Operate. HOT responses gated by SOF IG Status=Operate. Runway Friction rides standard Component Control (Class=GlobalTerrainSurface, ID=100) instead of the retired 0xCB user-defined packet.
- **IOS Backend** (FastAPI + rclpy) bridges ROS2 ↔ WebSocket. Run manually, not in launch file. Electrical config parser supports both v1 (flat sections) and v2 (nodes + connections) YAML formats.
- **IOS Frontend** (React + Zustand + WebSocket). URL routing: `/` = IOS app, `/cockpit/c172/*` = virtual cockpit panels. Dynamic A/C page and StatusStrip radio row driven by aircraft `navigation.yaml`. Interactive CBs on IOS (3-state: IN/POPPED/LOCKED with FORCE checkbox) and virtual cockpit (horizontal CB row). CBs flow through same `switch_ids[]` command path as switches.

### Avionics message conventions
- **Three tiers:** RawAvionicsControls → AvionicsControls → NavigationState
- **Frequency naming:** `_freq_mhz` / `_freq_khz` — no bare `_freq`
- **Wire keys** (WS): `com1_mhz`, `nav1_mhz`, `adf1_khz` etc.
- **Store keys** (Zustand): `com1Mhz`, `nav1Mhz`, `adf1Khz` etc.

### Implemented (functional)
- `sim_manager` — clock, state machine, heartbeat monitoring, CMD_REPOSITION with terrain wait
- `input_arbitrator` — 4-channel arbitration (flight, engine, avionics, panel)
- `flight_model_adapter` — JSBSim adapter, CapabilityMode, writeback (electrical, fuel, atmosphere), terrain refinement via RunIC, pending_ic_ gating. JSBSimAtmosphereWriteback: wind NED (fps), delta-T (Rankine), pressure (psf) into JSBSim property tree each step.
- Runway friction: two-factor model (surface type × contamination), YAML-configurable, JSBSim writeback, XP visual wetness
- `weather_solver` — replaces atmosphere_node. ISA model + OAT/QNH deviation + altitude-interpolated wind layers (angular shortest-arc) + MIL-F-8785C Dryden turbulence (deterministic seed, altitude-scaled) + Oseguera-Bowles microburst wind field. 19 unit tests (5 Dryden, 7 solver, 7 microburst). Standalone libraries + ROS2 node wrapper.
- `cigi_bridge` — CIGI 3.3 host via `cigi_session` (CCL-wrapped). Multi-point HOT (3 gear points), IG Mode repositioning handshake, SOF + HAT/HOT Extended Response parsing. Atmosphere / Weather Control (global + regional) / Environmental Region Control / Component Control emission, dirty-flag gated. 15 round-trip tests cover every emitter and processor.
- `navaid_sim` — VOR/ILS/NDB/DME/Marker, terrain LOS, A424 + XP12 parsers. Airport/runway DB (SearchAirports/GetRunways/GetTerrainElevation services)
- `sim_electrical` — pluginlib. C172 on **GraphSolver** (elec_graph): graph topology (39 nodes, 38 connections), unified connection model, failure effects (force/jam/disable/multiply), YAML validation, interactive CBs on IOS + cockpit, 15 standalone unit tests. EC135 stubbed (no-op, awaiting v2 YAML). Old ElectricalSolver deleted. Capability gating + writeback.
- `sim_engine_systems` — pluginlib → IEnginesModel. C172 piston + EC135 turboshaft plugins. EngineCommands published (zeros for current aircraft, pre-wired for turboprop/FADEC).
- `sim_fuel` — pluginlib → IFuelModel. C172 on **FuelGraphSolver** (fuel_graph): graph topology (7 nodes, 7 connections, 1 selector group), Layer 1 topological BFS. Fuel valve OFF → engine starvation via writeback (JSBSim tanks zeroed). Pump failures → starvation unless electric backup. 15 standalone unit tests. EC135 on flat solver (unchanged). Writeback starvation: display FuelState shows real quantities, writeback zeros tanks when engine starved.
- `sim_failures` — failure catalog from YAML (ATA grouped), armed triggers (delay/condition), routing via `/sim/failures/route/<handler>` (flight_model/electrical/air_data/gear). params_override_json for runtime params. Ground station failures are world conditions routed via `/world/navaids/command` (not yet implemented).
- `sim_navigation` — GPS/VOR/ILS/ADF/DME, CDI/TO-FROM, DME source selection, failure gating. No pluginlib.
- `sim_gear` — pluginlib → IGearModel. C172 fixed-tricycle. WoW per leg, nosewheel angle, brake echo.
- `sim_air_data` — pluginlib → IAirDataModel. Pitot-static with icing, turbulence noise, alternate static. C172 plugin.
- `ios_backend` — dynamic node discovery, FDM/fuel/nav/electrical/sim state WS forwarding. Panel + avionics commands. Failure catalog + injection/clear/clear_all. Navaid search API. Electrical config parser supports v1 + v2 YAML formats.
- `ios_frontend` — map, status strip (dynamic radio row), 9 panel tabs, action bar. Electrical switches (FORCE), ground services (tri-state), radio tuning. Failure panel with ATA grouping + navaid search. REPOSITIONING badge + button lockout. Dynamic A/C page from navigation.yaml. CB pull/reset/lock on IOS A/C page and virtual cockpit panel.
- `xplanecigi plugin` — CIGI 3.3 IG plugin for X-Plane 12 via `cigi_session::IgSession` (CCL cross-compiled for mingw64). Implements all 7 processor interfaces (IgCtrl, EntityCtrl, HatHotReq, Atmosphere, EnvRegion, WeatherCtrl, CompCtrl). Outbound via `BeginFrame` (SOF) + `AppendHatHotXResp`. IG Mode-driven terrain probing (4×0.5s stability). SOF Standby→Operate transitions. Weather → XP region datarefs (cloud/wind/vis/temp/precip) via 1 Hz flight loop. Runway friction decoded from Component Control (Class=8, ID=100).

### Active gap — weather injection
RESOLVED (implemented) — weather_solver_node + JSBSimAtmosphereWriteback + CIGI weather encoder + xplanecigi weather decoder. Dryden turbulence (MIL-F-8785C) and Oseguera-Bowles microburst model operational. 24 packages, 19 solver tests.

### Stub / scaffold only
- `microros_bridge` — skeleton lifecycle node
- `sim_hydraulic`, `sim_ice_protection`, `sim_pressurization` — heartbeat only, no solver
- Virtual cockpit avionics page — placeholder

### Known bugs (see bugs.md)
Architecture audit complete (2026-03-25). All bugs resolved (#1–#9). Bug #8 (FORCE checkbox) fixed — AircraftPanel now reads `forcedSwitchIds` from ArbitrationState round-trip instead of local React state. Dead wiring cleanup complete: removed 5 dead `/sim/failures/active` subscriptions, removed `/sim/failure/navaid_commands` publisher, removed unused `InitialConditions` import from ios_backend.

### Not yet implemented
- IOS: COM/NAV freq entry, flight departure/arrival graphs, debrief
- SimSnapshot save/load (designed, deferred)
- QTG test runner
- Audio system (RPi5/libpd architecture decided, not started)
- Remaining systems: hydraulic, ice_protection, pressurization (stubs exist)
- micro-ROS hardware bridge
- Electrical: selector + potentiometer connection types (designed, not in solver). Ammeter reads total bus current instead of alternator output.
- Ground station failure commands (IOS→navaid_sim via `/world/navaids/command`)
- Realistic cockpit panel layouts (C172 analog, future G1000): absolute-positioned
  sections over a panel background image. Reuses existing embeddable sections
  (ElectricalSection, RadioSection, etc.) with a layout wrapper per aircraft.
- Reusable instrument library: SVG gauge components (airspeed, altimeter, VSI,
  attitude, HSI, turn coordinator, engine gauges, annunciators). Config-driven
  via a new per-aircraft instruments.yaml defining ranges, color arcs, redlines.
  Shared between analog panels, G1000 displays, IOS displays. Resolution-
  independent and input-agnostic per the G1000 architecture decision.
- Frontend JSX audit: review for file length, inline style cleanup, component
  decomposition, prop drilling vs store access. Scheduled AFTER the instrument
  library is built (will naturally rewrite engine gauge sections).

### On the horizon / Immediate next design work
- IOS WeatherPanel redesign (cloud layers UI, airport station reference,
  altitude visualization, XP-inspired layout)

### Open decisions
- [x] CIGI library: raw encoding or cigicl? — **RESOLVED 2026-04-23: cigicl on both sides via `cigi_session` library**
- [ ] micro-ROS transport: serial UART or CAN?
- [ ] IOS auth: single-user or multi-role?
- [ ] Scenario file format: custom YAML or standard?
- [ ] IG manager: lifecycle node on remote hardware?
- [ ] EC135 electrical v2 YAML (blocked on Helisim license)

---

## CHANGE LOG
<!-- Append-only. Never edit past entries. New entries go at the bottom. -->
<!-- FORMAT: ## YYYY-MM-DD — [Claude Chat | Claude Code]  -->
<!--         - DECIDED / REASON / AFFECTS                  -->

## 2026-03-08 — Claude Chat

- DECIDED: ROS2 Jazzy as middleware backbone
- REASON: LTS, best real-time ecosystem, native sim time, rosbag replay built-in
- AFFECTS: all nodes, CMakeLists.txt, package.xml

- DECIDED: Topic namespace convention — `/devices/` for all external sources, `/sim/` for all internal state
- REASON: clear boundary between what the sim computes vs what feeds into it; snake_case throughout
- AFFECTS: all topic definitions in CLAUDE.md

- DECIDED: `input_arbitrator` is the single and only node that subscribes to `/devices/` topics
- REASON: clean separation; hardware fallback, instructor override, and virtual panels all handled in one place
- AFFECTS: input_arbitrator node, all system nodes (must NOT subscribe to /devices/)

- DECIDED: Input arbitration priority order per channel: INSTRUCTOR > HARDWARE > VIRTUAL > FROZEN
- REASON: matches real simulator instructor override semantics
- AFFECTS: input_arbitrator implementation

- DECIDED: Hardware health timeout = 500ms → auto-fallback to VIRTUAL
- REASON: chosen as conservative threshold for FNPT II; can be tuned in config
- AFFECTS: input_arbitrator implementation

- DECIDED: `/sim/world/` namespace for atmosphere, weather, nav_signals, terrain
- REASON: systems nodes must never compute environmental state themselves; shared infrastructure
- AFFECTS: atmosphere_node, nav_sim external service, terrain_service, all system nodes

- DECIDED: `nav_sim` is a separate external service/repo, publishes to `/sim/world/nav_signals`
- REASON: navaid simulation (DTED LOS, database, frequency tuning) is its own substantial project
- AFFECTS: navigation_node (consumer only), nav_sim repo (not yet started)

- DECIDED: ROS2 native sim time — Sim Manager publishes `/clock` at 50 Hz, all nodes use `use_sim_time: true`
- REASON: all ROS2 tooling (rosbag, rviz, rqt) understands it natively; no custom clock needed
- AFFECTS: sim_manager, all nodes (launch config)

- DECIDED: CGF = scripted entities in scenario YAML files; live IOS CGF control panel deferred
- REASON: covers FNPT II requirements without extra complexity at this stage
- AFFECTS: scenario_editor tooling, IOS frontend (CGF panel deferred)

- DECIDED: `FdmState.msg` field set and structure locked
- REASON: cross-referenced against Helisim ICD (743-0507), JSBSim FGPropagate/FGAuxiliary,
  X-Plane datarefs, DIS IEEE 1278 Entity State PDU, SISO RPR FOM 2.0, ARINC 429 label assignments
- AFFECTS: sim_msgs/msg/FdmState.msg, flight_model_adapter, cigi_bridge, navigation_node,
  all system nodes that subscribe to /sim/fdm/state

- DECIDED: FdmState uses quaternion as primary orientation (q_w/x/y/z), Euler angles derived
- REASON: numerically stable, no gimbal lock, matches JSBSim internal representation
- AFFECTS: FdmState.msg, cigi_bridge (must convert to Euler for CIGI), IOS display

- DECIDED: FdmState carries position in both ECG (lat/lon/alt) and ECEF (x/y/z)
- REASON: ECG for nav/QTG/IOS map, ECEF for CIGI IG and DIS/RPR FOM interoperability
- AFFECTS: FdmState.msg, flight_model_adapter (must compute both)

- DECIDED: FdmState carries velocity in NED, Body, and ECEF frames
- REASON: NED for instruments/QTG, Body for aero, ECEF for DIS dead-reckoning; no node should recompute frame transforms
- AFFECTS: FdmState.msg, flight_model_adapter

- DECIDED: Engine array size = 4, gear array size = 5, fuel tank array size = 4
- REASON: covers EC135 (2 engines), most twins, tricycle/tailwheel/quadricycle/helicopter skids
- AFFECTS: FdmState.msg

- DECIDED: Rotor fields gated by `is_helicopter` capability flag in FdmState
- REASON: fixed-wing adapter zero-fills rotor fields; nodes check flag before using them
- AFFECTS: FdmState.msg, flight_model_adapter, any node consuming rotor state

- DECIDED: HelisimUDPAdapter to be added as a fourth FDM adapter implementation
- REASON: Helisim 6.0 ICD (doc 743-0507) documents the full UDP export buffer;
  allows a running Helisim instance to be used as the FDM engine behind this framework
- AFFECTS: flight_model_adapter/ (new adapter class), CLAUDE.md FDM adapter section

- DECIDED: DECISIONS.md is append-only shared log; Claude Code must append an entry
  for every structural decision made during a session
- REASON: Claude Chat and Claude Code share no context; this file is the bridge
- AFFECTS: workflow convention; referenced in CLAUDE.md

## 2026-03-08 — Claude Code

- DECIDED: Package names for systems nodes use `sim_` prefix (sim_fuel, sim_electrical, etc.)
  not `systems_` prefix
- REASON: already established in prior scaffold commit, consistent with CLAUDE.md, colcon-verified
- AFFECTS: all systems/ package.xml, CMakeLists.txt, launch file

- DECIDED: `ios_backend` is an ament_python ROS2 package at ios/backend/
- REASON: needs rclpy for ROS2 bridge; FastAPI will be added later but ROS2 node structure needed now
- AFFECTS: ios/backend/package.xml, setup.py, ios_backend_node.py

- DECIDED: All stub nodes set `use_sim_time: true` as a parameter override in their constructor
- REASON: CLAUDE.md mandates all nodes use sim time; baking it into the node default ensures
  compliance even without launch file parameter injection
- AFFECTS: all 15 C++ node stubs, 2 Python node stubs

- DECIDED: `ControlsState.msg` and `FailureState.msg` added as single-field placeholders
- REASON: needed for scaffold completeness; full definitions deferred until node implementation
- AFFECTS: sim_msgs/msg/, sim_msgs/CMakeLists.txt

- DECIDED: Launch file lives at top-level `launch/sim_full.launch.py` (not inside a package)
- REASON: workspace-level integration test; launches all 17 nodes; validates scaffold completeness
- AFFECTS: launch/sim_full.launch.py

- DECIDED: Existing message files with full content (FdmState, SimState, AtmosphereState,
  ArbitrationState, WeatherState, etc.) are preserved — not overwritten with placeholders
- REASON: these were already designed and cross-referenced against ICDs in prior sessions
- AFFECTS: sim_msgs/msg/

## 2026-03-08 — Claude Code

- DECIDED: sim_manager publishes `/clock` at 50 Hz via wall timer, not sim timer
- REASON: sim_manager IS the clock source — cannot use sim time to drive its own clock
- AFFECTS: sim_manager_node.cpp (wall timers), launch file (use_sim_time: false)

- DECIDED: sim_manager state machine uses validated transitions with explicit allowed edges
- REASON: prevents invalid state transitions; matches CLAUDE.md state diagram
  (INIT→READY→RUNNING↔FROZEN, RUNNING/FROZEN→RESETTING→READY, any→SHUTDOWN)
- AFFECTS: sim_manager_node.cpp transition_to()

- DECIDED: Heartbeat monitoring uses 2-second timeout; auto-freeze on required node loss
- REASON: conservative threshold for safety-critical sim; nodes must publish at 1 Hz
- AFFECTS: sim_manager_node.cpp check_node_health(), aircraft config required_nodes list

- DECIDED: RESETTING state uses a 100ms wall timer before transitioning to READY
- REASON: gives downstream nodes time to receive the IC broadcast before sim resumes
- AFFECTS: sim_manager_node.cpp begin_reset()

- DECIDED: nlohmann/json vendored as single header at core/sim_manager/include/sim_manager/nlohmann/json.hpp
- REASON: system package (nlohmann-json3-dev) requires sudo; vendoring avoids system dependency
- AFFECTS: sim_manager CMakeLists.txt (include path), sim_manager_node.cpp

- DECIDED: Aircraft config search path cascade: parameter → workspace-relative → installed share
- REASON: supports both development (colcon workspace) and installed (share directory) workflows
- AFFECTS: sim_manager_node.cpp load_aircraft_config()

- DECIDED: New message types added: SimCommand, SimAlert, InitialConditions, ScenarioEvent
- REASON: needed for IOS→sim_manager command interface, alert broadcasting, IC/scenario handling
- AFFECTS: sim_msgs/msg/, sim_msgs/CMakeLists.txt

## 2026-03-08 — Claude Chat

- DECIDED: All system nodes will be implemented as lifecycle nodes (rclcpp_lifecycle)
- REASON: enables DSIM-style hot-reload per node without process restart;
  Sim Manager can drive configure/activate/deactivate/cleanup transitions per node
- AFFECTS: all nodes in systems/, core/ (except sim_manager itself),
  sim_manager (add CMD_RELOAD_NODE to SimCommand, lifecycle service clients)

- DECIDED: CMD_RELOAD_NODE added to SimCommand enum
- REASON: IOS status page needs per-node reload button; Sim Manager owns the lifecycle transitions
- AFFECTS: sim_msgs/msg/SimCommand.msg, sim_manager_node.cpp, IOS status page

## 2026-03-08 — Claude Chat

- DECIDED: Aircraft-specific logic lives entirely in per-aircraft ROS2 packages (aircraft/c172/, aircraft/ec135/)
- REASON: adding a new aircraft must not require modifying any system node package;
  all system nodes contain zero aircraft-specific code
- AFFECTS: all systems/ nodes, aircraft/ directory structure, CMakeLists.txt files

- DECIDED: System nodes load aircraft behavior via pluginlib at runtime
- REASON: pluginlib allows plugin discovery across packages without recompiling the host node;
  aircraft package registers its plugins, node loads correct .so from aircraft_id parameter
- AFFECTS: sim_electrical, sim_fuel, sim_engines, sim_gear, sim_hydraulic, sim_navigation,
  each gets an abstract interface (IElectricalModel, IFuelModel, etc.)

- DECIDED: Each aircraft package is a standalone ROS2 package (own package.xml, CMakeLists.txt)
- REASON: allows install via colcon build --packages-select aircraft_c172; no other package touched
- AFFECTS: aircraft/c172/, aircraft/ec135/ directory structure

- DECIDED: Aircraft YAML config files (electrical.yaml, fuel.yaml, etc.) live inside the aircraft package
- REASON: data and logic for an aircraft travel together; system nodes load YAML path from aircraft package share directory
- AFFECTS: aircraft/*/  config layout, system node config loading

## 2026-03-08 — Claude Code

- DECIDED: All 14 stub nodes converted to rclcpp_lifecycle::LifecycleNode with
  on_configure/on_activate/on_deactivate/on_cleanup callbacks
- REASON: implements Claude Chat decision for DSIM-style hot-reload;
  enables CMD_RELOAD_NODE from IOS without process restart
- AFFECTS: all .cpp files in systems/, core/ (except sim_manager), hardware/

- DECIDED: Lifecycle nodes auto-activate on startup via trigger_transition() in a 100ms timer
- REASON: avoids chicken-and-egg: heartbeats require active state, sim_manager needs heartbeats
  for INIT→READY. Auto-activation preserves existing startup flow while supporting lifecycle
- AFFECTS: all lifecycle node constructors

- DECIDED: LifecyclePublisher requires explicit on_activate()/on_deactivate() calls when using
  trigger_transition() (ROS2 only manages publishers automatically through service interface)
- REASON: discovered during testing — trigger_transition() does not call publishers' on_activate;
  explicit calls needed for heartbeat publishing to work with auto-start pattern
- AFFECTS: all lifecycle node on_activate/on_deactivate methods

- DECIDED: System node names aligned with heartbeat names (e.g., "sim_fuel" not "fuel_node")
- REASON: lifecycle services use node name as prefix (/<node_name>/change_state);
  matching heartbeat names lets sim_manager use required_nodes list for both monitoring and reload
- AFFECTS: launch file node names, all system node constructors

- DECIDED: CMD_RELOAD_NODE = 7 added to SimCommand.msg; sim_manager chains
  deactivate→cleanup→configure→activate via async lifecycle service calls
- REASON: implements Claude Chat decision; payload_json contains {"node_name": "sim_fuel"}
- AFFECTS: sim_msgs/msg/SimCommand.msg, sim_manager_node.cpp reload_node()

## 2026-03-08 — Claude Code

- DECIDED: sim_interfaces package created as headers-only with 5 abstract interfaces:
  IElectricalModel, IFuelModel, IEnginesModel, IGearModel, INavigationModel
- REASON: implements Claude Chat pluginlib decision; pure abstract classes with
  configure(yaml_path), update(dt_sec), apply_failure(id, active) methods
- AFFECTS: core/sim_interfaces/, all system nodes, all aircraft plugin packages

- DECIDED: 5 system nodes (electrical, fuel, engines, gear, navigation) refactored to use
  pluginlib::ClassLoader — plugin loaded in on_configure(), model->update(dt) at 50 Hz in on_activate()
- REASON: zero aircraft-specific code in systems/; aircraft_id parameter drives plugin name
  construction as "aircraft_" + aircraft_id + "::XxxModel"
- AFFECTS: systems/{electrical,fuel,engines,gear,navigation}/src/*.cpp, CMakeLists.txt, package.xml

- DECIDED: aircraft_c172 and aircraft_ec135 packages created with stub plugin implementations,
  plugins.xml registration, and per-system YAML config files
- REASON: demonstrates two aircraft types (fixed-wing + rotary) using same framework interfaces
- AFFECTS: aircraft/c172/, aircraft/ec135/ (CMakeLists.txt, package.xml, plugins.xml, src/, config/)

- DECIDED: Launch file passes aircraft_id parameter to the 5 pluginlib system nodes;
  remaining 4 system nodes (hydraulic, failures, ice_protection, pressurization) do not yet use pluginlib
- REASON: only nodes with abstract interfaces get the parameter; others remain simple lifecycle stubs
- AFFECTS: launch/sim_full.launch.py

## 2026-03-08 — Claude Code

- DECIDED: JSBSim integrated via CMake FetchContent (v1.2.1) as a static library
- REASON: libJSBSim-dev not available via apt on this system; FetchContent fetches and builds
  JSBSim from GitHub, links the `libJSBSim` static library target
- AFFECTS: core/flight_model_adapter/CMakeLists.txt

- DECIDED: IFdmAdapter abstract interface defined with initialize(), apply_initial_conditions(),
  step(), get_state() methods — signature differs from CLAUDE.md IFlightModel sketch
- REASON: CLAUDE.md sketch used AircraftConfig/ControlsInput/SimCapabilities types that don't exist yet;
  IFdmAdapter uses aircraft_id string + InitialConditions.msg + FdmState.msg directly
- AFFECTS: core/flight_model_adapter/include/flight_model_adapter/IFdmAdapter.hpp

- DECIDED: JSBSimAdapter maps C172P model ("c172" aircraft_id → "c172p" JSBSim model name)
- REASON: JSBSim ships its C172 model as "c172p" (Cessna 172P); the mapping is in initialize()
- AFFECTS: core/flight_model_adapter/src/JSBSimAdapter.cpp

- DECIDED: JSBSim data path resolved at compile time via JSBSIM_ROOT_DIR define from FetchContent source dir
- REASON: JSBSim needs aircraft/, engine/, systems/ data directories; the FetchContent source tree
  contains these. Runtime override via `jsbsim_root_dir` parameter for custom installations
- AFFECTS: core/flight_model_adapter/CMakeLists.txt, flight_model_adapter_node.cpp

- DECIDED: Default IC applied during initialize() (KJFK, cold and dark, on ground)
- REASON: JSBSim asserts on FGTable::GetValue if Run() is called without valid IC;
  default IC ensures the FDM is in a valid state immediately after model load
- AFFECTS: core/flight_model_adapter/src/JSBSimAdapter.cpp

- DECIDED: FDM adapter uses wall timer (not sim timer) for its 50 Hz update loop
- REASON: the adapter must run independently of sim state; it drives JSBSim forward and publishes
  FdmState regardless of whether sim_manager has advanced to RUNNING state
- AFFECTS: core/flight_model_adapter/src/flight_model_adapter_node.cpp

- DECIDED: ECEF position and velocity computed in the adapter (not deferred to consumers)
- REASON: DECISIONS.md/CLAUDE.md mandate FdmState carries both ECG and ECEF;
  WGS-84 geodetic-to-ECEF and NED-to-ECEF rotation implemented in JSBSimAdapter
- AFFECTS: core/flight_model_adapter/src/JSBSimAdapter.cpp

- DECIDED: Quaternion NED-to-Body constructed from JSBSim Euler angles (phi/theta/psi)
- REASON: JSBSim's internal quaternion is ECI-to-Body, not the NED-to-Body that FdmState requires;
  constructing from Euler ZYX gives the correct NED-to-Body quaternion
- AFFECTS: core/flight_model_adapter/src/JSBSimAdapter.cpp get_state()

## 2026-03-08 — Claude Code

- DECIDED: elec_sys_sim solver gains loadTopologyYaml() method alongside existing JSON loader
- REASON: aircraft configs use YAML per framework convention; solver is reused without rewrite
- AFFECTS: systems/elec_sys_sim/include/elec_sys_sim/electrical_solver.hpp,
  systems/elec_sys_sim/src/electrical_solver.cpp (yaml-cpp dependency added)

- DECIDED: Electrical topology defined in YAML format at aircraft/*/config/electrical.yaml
- REASON: full solver topology (sources, buses, switches, loads, CAS) expressed in the same
  YAML config pattern used by all other aircraft subsystems
- AFFECTS: aircraft/c172/config/electrical.yaml, aircraft/ec135/config/electrical.yaml

- DECIDED: C172 electrical model: single alternator, single battery, 4 buses
  (primary, avionics, essential, hot battery), 21 loads, split master switch
- REASON: matches C172S POH Section 7 electrical system architecture
- AFFECTS: aircraft/c172/config/electrical.yaml

- DECIDED: EC135 electrical model: 2 starter/generators, APU gen, ext power, 2 batteries,
  7 buses, 17 loads — ported from elec_sys_sim twin_helo_electrical.json reference
- REASON: twin-engine helicopter topology already validated in elec_sys_sim standalone testing
- AFFECTS: aircraft/ec135/config/electrical.yaml

- DECIDED: Aircraft electrical_model.cpp plugins delegate entirely to ElectricalSolver;
  configure() calls loadTopologyYaml(), update() calls step(), apply_failure() maps to
  injectFault()/clearFault() with "target_id/fault_type" format
- REASON: reuses existing solver logic; aircraft plugins contain zero solver code
- AFFECTS: aircraft/c172/src/electrical_model.cpp, aircraft/ec135/src/electrical_model.cpp,
  aircraft CMakeLists.txt and package.xml (now depend on sim_electrical)

## 2026-03-08 — Claude Code

- DECIDED: Deleted systems/elec_sys_sim/; solver library moved into systems/electrical/ (sim_electrical package)
- REASON: elec_sys_sim was a standalone dev tool; solver now lives alongside the electrical node
  as a shared library (`electrical_solver`) exported by sim_electrical
- AFFECTS: systems/electrical/ (gains include/electrical/electrical_solver.hpp, src/electrical_solver.cpp,
  nlohmann/json + yaml-cpp deps), aircraft/c172 and aircraft/ec135 (depend on sim_electrical,
  include path changed to electrical/electrical_solver.hpp)

## 2026-03-08 — 21:55:42 - Claude Code

- DECIDED: atmosphere_node implemented — ISA model (ICAO Doc 7488), two layers
  (troposphere 0–11km, tropopause 11–20km), publishes /sim/world/atmosphere at 10 Hz
- REASON: build order step 5; systems nodes need atmosphere data for density altitude, OAT, etc.
- AFFECTS: core/atmosphere_node/src/atmosphere_node.cpp

- DECIDED: AtmosphereState.msg updated — added oat_k field (ISA temp + deviation),
  removed isa_temperature_dev_k, reordered for clarity
- REASON: consumers need actual OAT directly; ISA baseline is temperature_k, actual is oat_k
- AFFECTS: sim_msgs/msg/AtmosphereState.msg

- DECIDED: WeatherState.msg updated — oat_degc replaced with oat_deviation_k (K),
  qnh_hpa replaced with qnh_pa (Pa), added std_msgs/Header
- REASON: ISA deviation convention (ISA+10, ISA-5) is standard for flight sim weather injection;
  Pa units consistent with rest of framework; header needed for timestamping
- AFFECTS: sim_msgs/msg/WeatherState.msg

## 2026-03-08 — Claude Code

- DECIDED: input_arbitrator fully implemented — per-channel source selection, hardware health
  monitoring, freeze support, panel on-change publishing
- REASON: build order step 6; the single gateway for all /devices/ → /sim/controls/ data flow
- AFFECTS: core/input_arbitrator/src/input_arbitrator_node.cpp

- DECIDED: std_msgs/Header added to all controls messages (RawFlightControls, RawEngineControls,
  RawAvionicsControls, FlightControls, EngineControls, AvionicsControls)
- REASON: timestamping needed for latency tracking and heartbeat-based health monitoring
- AFFECTS: sim_msgs/msg/{Raw,}FlightControls.msg, {Raw,}EngineControls.msg, {Raw,}AvionicsControls.msg

- DECIDED: PanelControls.msg created — switch_ids/switch_states + selector_ids/selector_values
- REASON: panel state (cockpit switches, rotary selectors) needs its own message type;
  published on /devices/*/panel (input) and /sim/controls/panel (on-change output)
- AFFECTS: sim_msgs/msg/PanelControls.msg, sim_msgs/CMakeLists.txt

- DECIDED: ArbitrationState.msg redesigned — uint8 per-channel source with SOURCE_* constants,
  per-channel hardware health bools (replaces old string-array placeholder)
- REASON: fixed fields are more efficient than dynamic arrays for 4 known channels
- AFFECTS: sim_msgs/msg/ArbitrationState.msg

- DECIDED: DeviceHeartbeat.msg redesigned — per-channel bool fields (flight_ok, engine_ok,
  avionics_ok, panel_ok) replace old dynamic channel_names/channel_healthy arrays
- REASON: matches the 4 fixed control channels; simpler for arbitrator to consume
- AFFECTS: sim_msgs/msg/DeviceHeartbeat.msg

- DECIDED: Instructor takeover is sticky — once instructor publishes on a channel, source
  stays INSTRUCTOR until node reconfigure. No auto-release, no timeout.
- REASON: safety; avoids unexpected source jolt if instructor momentarily stops publishing
- AFFECTS: input_arbitrator source selection logic

## 2026-03-08 — Claude Code

- DECIDED: Panel control ID naming convention adopted — prefixes: sw_ (boolean switch),
  btn_ (momentary button), cb_ (circuit breaker), sel_ (detented rotary selector),
  pot_ (analog potentiometer), enc_abs_ (absolute encoder), enc_rel_ (relative encoder)
- REASON: consistent, unambiguous identification of control type from ID string alone
- AFFECTS: aircraft/c172/config/electrical.yaml, aircraft/ec135/config/electrical.yaml,
  all future YAML configs and panel control references

- DECIDED: All electrical YAML switch IDs renamed to sw_ prefix convention
  (e.g. alt_switch→sw_alt, batt_switch→sw_battery, gen1_contactor→sw_gen1,
  bus_tie→sw_bus_tie, stby_batt_sw→sw_stby_batt, avionics_master→sw_avionics_master)
- REASON: consistent naming; solver loads IDs dynamically from YAML so no C++ changes needed
- AFFECTS: aircraft/c172/config/electrical.yaml, aircraft/ec135/config/electrical.yaml

- DECIDED: RawFlightControls.msg and FlightControls.msg gain brake_left/brake_right (float32)
- REASON: toe brakes are a fundamental flight control input for taxi/landing
- AFFECTS: sim_msgs/msg/RawFlightControls.msg, sim_msgs/msg/FlightControls.msg,
  input_arbitrator (must forward new fields)

- DECIDED: PanelControls.msg extended with pot_ids/pot_values, encoder_abs_ids/encoder_abs_values,
  encoder_rel_ids/encoder_rel_deltas arrays
- REASON: panel controls now cover all 7 device types in the naming convention
- AFFECTS: sim_msgs/msg/PanelControls.msg, input_arbitrator, virtual_panels, microros_bridge

- DECIDED: FuelState.msg replaced from placeholder to full fixed-array format:
  4-element arrays for tank_quantity_kg, tank_quantity_pct, tank_usable_kg,
  engine_fuel_flow_kgs, fuel_pressure_pa, boost_pump_on, tank_selected;
  plus total_fuel_kg, total_fuel_pct, low_fuel_warning, cg_contribution_m
- REASON: matches FdmState array convention (4 tanks max); covers FNPT II fuel panel requirements
- AFFECTS: sim_msgs/msg/FuelState.msg, sim_fuel node, aircraft fuel plugins

- DECIDED: IFuelModel interface updated — update() now takes engine_fuel_flow_kgs, PanelControls,
  active_failures; added apply_initial_conditions(fuel_total_pct) and get_state() → FuelState
- REASON: fuel model needs FDM flow data, panel switch/selector state, and failure awareness
- AFFECTS: core/sim_interfaces/include/sim_interfaces/i_fuel_model.hpp, all aircraft fuel plugins

- DECIDED: sim_interfaces package now depends on sim_msgs
- REASON: IFuelModel interface includes sim_msgs/msg/FuelState.hpp and PanelControls.hpp
- AFFECTS: core/sim_interfaces/CMakeLists.txt, core/sim_interfaces/package.xml

- DECIDED: sim_fuel node fully implemented — lifecycle node with subscriptions to
  /sim/fdm/state, /sim/state, /sim/controls/panel, /sim/failures/active, /sim/initial_conditions;
  publishes /sim/fuel/state at configurable rate (default 50 Hz); respects FROZEN state
- REASON: build order step 7; first full system node with complete data flow
- AFFECTS: systems/fuel/src/fuel_node.cpp

- DECIDED: C172 fuel model: 2 tanks (left_wing 92kg, right_wing 92kg), sel_fuel selector
  with positions both/left/right/off, single boost pump sw_fuel_pump
- REASON: matches C172S POH fuel system
- AFFECTS: aircraft/c172/config/fuel.yaml, aircraft/c172/src/fuel_model.cpp

- DECIDED: EC135 fuel model: 1 tank (main_tank 738kg), no selector, 2 boost pumps
  (sw_fuel_pump_l, sw_fuel_pump_r), both engines feed from single tank
- REASON: matches EC135 fuel system architecture
- AFFECTS: aircraft/ec135/config/fuel.yaml, aircraft/ec135/src/fuel_model.cpp

- DECIDED: Aircraft plugin packages (aircraft_c172, aircraft_ec135) now depend on
  sim_msgs and yaml-cpp for fuel model implementation
- REASON: fuel plugins load YAML config and return FuelState messages
- AFFECTS: aircraft/c172/CMakeLists.txt, aircraft/c172/package.xml,
  aircraft/ec135/CMakeLists.txt, aircraft/ec135/package.xml

## 2026-03-09 — Claude Code

- DECIDED: ios_backend implemented as FastAPI + rclpy WebSocket bridge on port 8080
- REASON: build order steps 8+9 combined; virtual fuel panel needs a WebSocket bridge
  to reach ROS2 topics from a browser
- AFFECTS: ios/backend/ios_backend/ios_backend_node.py

- DECIDED: ios_backend threading model: rclpy spins in background thread,
  FastAPI/uvicorn runs in main thread, asyncio.Queue bridges ROS2→WebSocket direction
- REASON: rclpy.spin() is blocking; must run in separate thread from async FastAPI event loop
- AFFECTS: ios/backend/ios_backend/ios_backend_node.py

- DECIDED: WebSocket protocol: JSON messages with {topic, data} structure;
  server→client for state updates, client→server for panel commands
- REASON: simple, human-readable, matches ROS2 topic structure; no binary protocol needed at this stage
- AFFECTS: ios/backend/ios_backend/ios_backend_node.py, ios/frontend/fuel_panel.html

- DECIDED: ios_backend subscribes to /sim/fuel/state and /sim/state, publishes to
  /devices/virtual/panel — scoped to fuel panel needs only, not full IOS
- REASON: minimal scope for end-to-end data path validation; more subscriptions added later
- AFFECTS: ios/backend/ios_backend/ios_backend_node.py

- DECIDED: Virtual fuel panel implemented as single self-contained HTML file
  (ios/frontend/fuel_panel.html) — vanilla JS + WebSocket, no build step
- REASON: prototype for proving browser→ROS2 data path; React/npm deferred to full IOS build
- AFFECTS: ios/frontend/fuel_panel.html

- DECIDED: First virtual panel rendering confirmed as web-based (WebSocket → FastAPI → ROS2)
- REASON: validates the web rendering path from CLAUDE.md open decisions; other renderers
  (OpenGL, VR) can coexist on the same topics
- AFFECTS: CLAUDE.md open decisions (virtual panel rendering)

## 2026-03-09 — Claude Code

- DECIDED: ROS2 message fields must be explicitly cast to Python native types (float(), bool())
  before JSON serialization in ios_backend — numpy.float32/bool_ from rclpy are not JSON-serializable
- REASON: json.dumps() crashes silently on numpy scalar types, killing the WebSocket sender coroutine
- AFFECTS: ios/backend/ios_backend/ios_backend_node.py — all ROS2 callback → dict conversions

- DECIDED: Build order steps 8+9 (virtual fuel panel + IOS backend skeleton) completed and verified
  end-to-end: browser ↔ WebSocket ↔ FastAPI ↔ ROS2 data loop confirmed working
- REASON: proves the full device→arbitrator→sim→browser pipeline for virtual panels
- AFFECTS: build order progress — ready to proceed to step 10 (IOS frontend skeleton) or expand system nodes

## 2026-03-09 — Claude Code

- DECIDED: IOS frontend skeleton built as React + Vite + Zustand + react-leaflet app at ios/frontend/app/
- REASON: build order step 10; replaces vanilla HTML prototype with full responsive React app
- AFFECTS: ios/frontend/app/ (new), ios/backend/ios_backend_node.py (stub data additions)

- DECIDED: Responsive breakpoint at 1400px — below = tablet overlay mode, above = desktop side-by-side
- REASON: covers landscape tablet ~13" and 24"+ desktop displays
- AFFECTS: App.jsx useBreakpoint hook, SidePanel

- DECIDED: Avionics state is read-only in IOS — displays what pilot has tuned, no instructor override
- REASON: design decision from Claude Chat session; COM/NAV/ADF/XPDR monitoring only
- AFFECTS: avionics store state, StatusStrip Row 3, no InstructorRadioCommand message needed

- DECIDED: URL-based routing deferred — multi-screen/multi-tab support added in next IOS sprint
- REASON: single-app skeleton first; routing adds complexity before layout is validated
- AFFECTS: deferred to IOS sprint 2

- DECIDED: WebSocket protocol changed from {topic, data} format to {type, ...flat_fields} format
- REASON: simpler for frontend store dispatch; type field enables clean switch statement routing
- AFFECTS: ios/backend/ios_backend_node.py, useSimStore.js onmessage handler

- DECIDED: Backend sends stub data (fdm_state, avionics, node_health, failure_count) via broadcast
  to all connected clients every 2s using asyncio background task
- REASON: enables frontend visual testing without running full ROS2 node graph
- AFFECTS: ios/backend/ios_backend_node.py (broadcast(), send_stub_data())

## 2026-03-09 — Claude Code

- DECIDED: IOS frontend promoted from ios/frontend/app/ to ios/frontend/ (removed extra nesting)
- REASON: two frontend implementations existed (TypeScript skeleton at root + JSX app in app/);
  TypeScript version was unused and incomplete — consolidated to single JSX app at ios/frontend/
- AFFECTS: ios/frontend/ (all source now at root level), removed ios/frontend/app/

- DECIDED: Deleted unused TypeScript frontend skeleton (src/*.tsx, store.ts, hooks/*.ts,
  components/*.tsx, tsconfig*.json, vite.config.ts, root package.json/index.html)
- REASON: dead code; JSX app in app/ was the active implementation; TypeScript version had
  no map, minimal panels, separate store — never used in production
- AFFECTS: ios/frontend/ (removed: src/, dist/, .claude/, tsconfig*.json, vite.config.ts)

- DECIDED: fuel_panel.html retained as standalone test tool
- REASON: working vanilla JS prototype useful for testing WebSocket → ROS2 panel data path
  independently of the React app
- AFFECTS: ios/frontend/fuel_panel.html (unchanged)

- DECIDED: Fixed ios_backend broadcast() — added `global connected_clients` declaration
- REASON: Python augmented assignment (`connected_clients -= disconnected`) made Python treat
  module-level set as local variable, crashing send_stub_data() with UnboundLocalError
- AFFECTS: ios/backend/ios_backend/ios_backend_node.py

- DECIDED: ios_backend stub state machine responds to CMD_RUN/FREEZE/RESET for frontend testing
- REASON: frontend buttons were non-functional without state feedback loop; needed before real ROS2 wiring
- AFFECTS: ios/backend/ios_backend/ios_backend_node.py

## 2026-03-09 — Claude Chat / Claude Code

- DECIDED: When any non-map tab is active, the panel fills the entire content area — map is hidden
- REASON: map does not need to be visible at all times; cleaner UX, more panel space, simpler layout
- AFFECTS: App.jsx, SidePanel.jsx (removed overlay/breakpoint logic)

- DECIDED: Nav tabs redesigned — monospace symbols, fuel-panel dark aesthetic (#0a0e17 bg, #00ff88 accent)
- REASON: emoji icons were visually inconsistent with the instrument panel aesthetic
- AFFECTS: ios/frontend/src/components/NavTabs.jsx

- DECIDED: All Unicode escape sequences replaced with actual UTF-8 characters in frontend source
- REASON: escape sequences like \u00B0 rendered as literal text in JSX attribute strings
- AFFECTS: WeatherPanel.jsx, PositionPanel.jsx, FailuresPanel.jsx, ActionBar.jsx, NavTabs.jsx

- DECIDED: All panels restyled to dark industrial aesthetic matching electrical/fuel simulator palette
- REASON: panels looked generic (light grey inputs, flat white text); now consistent with instrument panel feel
- AFFECTS: PanelUtils.jsx, SidePanel.jsx, all panels/ components
  Palette: bg #0a0e17, panel #111827, elevated #1c2333, borders #1e293b,
  text #e2e8f0, dim #64748b, accent #00ff88, cyan headers #39d0d8,
  danger #ff3b30, pending #bc4fcb. Node status dots with glow.
  ATA rows in #111827 with #39d0d8 codes. Tank bars #00ff88/#ff3b30.

## 2026-03-09 — Claude Chat / Claude Code

- DECIDED: Node management made real — nodes discovered from /sim/diagnostics/heartbeat,
  lifecycle state tracked from /sim/diagnostics/lifecycle_state (std_msgs/String "name:state")
- REASON: previously all nodes were hardcoded stubs; RELOAD button did nothing
- AFFECTS: ios/backend/ios_backend_node.py, ios/frontend/src/components/panels/NodesPanel.jsx,
  ios/frontend/src/store/useSimStore.js

- DECIDED: CMD_DEACTIVATE_NODE=8, CMD_ACTIVATE_NODE=9, CMD_RESET_NODE=10 added to SimCommand.msg
- REASON: per-node pause/resume/reset needed for debugging individual subsystems during development
- AFFECTS: sim_msgs/msg/SimCommand.msg, sim_manager_node.cpp, ios_backend_node.py, useSimStore.js

- DECIDED: ios_backend calls ROS2 lifecycle services directly for node commands
- REASON: sim_manager already handles CMD_RELOAD_NODE via lifecycle; backend can also call
  services directly for ACTIVATE/DEACTIVATE without routing through sim_manager
- AFFECTS: ios/backend/ios_backend/ios_backend_node.py (lifecycle_msgs/srv/ChangeState client)

- DECIDED: node_health broadcast moved from 2s stub loop to dedicated 1s real-data task
- REASON: node health needs to update at 1Hz to reflect heartbeat timeouts accurately
- AFFECTS: ios/backend/ios_backend_node.py

## 2025-03-09 — 12:40:00 - Claude Code

- DECIDED: workspace reorganization — all ROS2 packages moved under src/
- REASON: standard colcon workspace layout; separates ROS2 packages from non-ROS2 files (frontend, launch, docs)
- AFFECTS: all package paths now src/core/, src/systems/, src/hardware/, src/sim_msgs/,
  src/aircraft/, src/qtg/, src/ios_backend/ (was ios/backend/). ios/frontend/ stays at top level.
  launch/ stays at top level. CLAUDE.md repo structure section updated.

- DECIDED: launch file aircraft_config_dir default changed from 'aircraft' to 'src/aircraft'
- REASON: workspace-relative path changed after reorg
- AFFECTS: launch/sim_full.launch.py, src/core/sim_manager/src/sim_manager_node.cpp (added src/aircraft/ to search paths)

- DECIDED: enhanced node management with 4-state model (OK/DEGRADED/LOST/OFFLINE)
- REASON: IOS should show expected-but-not-running nodes as OFFLINE, not just omit them
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py, ios/frontend/src/components/panels/NodesPanel.jsx

- DECIDED: KNOWN_SIM_NODES static list in ios_backend for expected framework nodes
- REASON: backend always includes these in node_health broadcasts so frontend can show OFFLINE state
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py

- DECIDED: ROS2 graph discovery via get_node_names_and_namespaces() as third discovery source
- REASON: catches nodes that are alive but haven't sent heartbeats yet; supplements heartbeat + lifecycle_state
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py (refresh_graph() task at 3s interval)

- DECIDED: node_health WS message now includes in_graph and known fields per node
- REASON: frontend can differentiate known-OFFLINE from discovered-dynamic, and show graph presence
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py, ios/frontend/src/store/useSimStore.js (passthrough)

## 2025-03-09 — 14:00:00 - Claude Code

- DECIDED: all C++ lifecycle nodes publish heartbeat to /sim/diagnostics/heartbeat as std_msgs/String
- REASON: nodes were publishing Header to /sim/heartbeat/<name> — wrong topic and type.
  ios_backend subscribes to /sim/diagnostics/heartbeat expecting String with node name as data.
- AFFECTS: all 13 lifecycle nodes under src/systems/, src/core/ (except sim_manager), src/hardware/

- DECIDED: all C++ lifecycle nodes publish lifecycle state to /sim/diagnostics/lifecycle_state
- REASON: enables ios_backend to track lifecycle transitions (active/inactive/unconfigured) per node
  without needing sim_manager as intermediary. Published as "node_name:state" format.
- AFFECTS: same 13 nodes. Each publishes on configure→inactive, activate→active,
  deactivate→inactive, cleanup→unconfigured.

- DECIDED: heartbeat and lifecycle_state publishers are rclcpp::Publisher (not LifecyclePublisher)
- REASON: must publish in all lifecycle states (including unconfigured/inactive).
  LifecyclePublisher only publishes when active — defeats the purpose of reporting state transitions.
- AFFECTS: all 13 lifecycle nodes

## 2025-03-09 — 15:00:00 - Claude Code

- DECIDED: sim_manager subscribes to /sim/diagnostics/heartbeat (single topic, String) instead of per-node /sim/heartbeat/<name> (Header)
- REASON: nodes now publish to /sim/diagnostics/heartbeat. sim_manager was stuck in INIT because
  it never received heartbeats on the old per-node topics.
- AFFECTS: src/core/sim_manager/src/sim_manager_node.cpp — replaced vector of per-node Header
  subscriptions with single String subscription that dispatches by node name

- DECIDED: ios_backend no longer filters itself from graph discovery
- REASON: was excluded via `if name != 'ios_backend'` in get_node_names(), causing it to always
  show as OFFLINE despite being in KNOWN_SIM_NODES
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py

- DECIDED: ios_backend publishes its own heartbeat (1 Hz) and lifecycle state to diagnostics topics
- REASON: Python node had no heartbeat publisher — only C++ nodes were updated. Without it,
  ios_backend showed as OFFLINE/LOST in the IOS node panel.
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py — added _heartbeat_pub, _lifecycle_pub,
  _heartbeat_timer (1 Hz), _publish_lifecycle('active') on startup

- DECIDED: node status logic uses heartbeat age as primary OK signal alongside in_graph
- REASON: heartbeat age < 2s should mean OK regardless of graph discovery timing.
  Previously only in_graph=True gave OK status, so nodes with fresh heartbeats but not yet
  graph-discovered showed as LOST (orange).
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py send_node_health() status logic:
  OK = in_graph OR heartbeat age < 2s, DEGRADED = age 2-5s, LOST = age 5-10s, OFFLINE = no data


## 2026-03-09 — 12:00:00 - Claude Code
- DECIDED: Infer lifecycle_state='active' from heartbeat reception in ios_backend
- REASON: C++ lifecycle nodes publish lifecycle state on transitions (configure→inactive,
  activate→active) during auto-start, which happens before ios_backend subscribes. By the
  time ios_backend is listening, no more transitions occur, so lifecycle_state stays 'unknown'.
  Since only active lifecycle nodes publish heartbeats, receiving a heartbeat implies 'active'.
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py _on_heartbeat() — sets
  lifecycle_state to 'active' for new entries and for existing entries still at 'unknown'

## 2026-03-09 — 16:00:00 - Claude Code
- DECIDED: WebSocket command handler wrapped in try/except with command_error response
- REASON: unhandled exceptions in lifecycle service calls (timeouts, unavailable services)
  crashed the WS message loop. Async lifecycle tasks (reload, activate, deactivate) were
  fire-and-forget with no error reporting. Now all errors are caught, logged server-side,
  and sent back to the client as {"type": "command_error", "cmd": <id>, "error": <str>}.
  No error state is set on the ios_backend node itself.
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py — call_lifecycle() now raises
  on failure instead of returning False, do_reload_node() propagates exceptions,
  _safe_lifecycle() wrapper catches and reports via WS, outer handler catches sync errors

## 2026-03-09 — 17:00:00 - Claude Code
- DECIDED: ios_backend subscribes to /sim/fdm/state (FdmState) and converts to fdm_state WS message
- REASON: FDM data was entirely stub — no real subscription existed. Now subscribes to
  /sim/fdm/state and converts with unit conversions: latitude/longitude_rad→deg,
  altitude_msl_m→ft, ias_ms/ground_speed_ms→kt, magnetic_heading_rad/ground_track_rad→deg,
  vertical_speed_ms→fpm (*196.85), pitch_rad/roll_rad→deg. FDM stub is now guarded by
  'fdm_state' not in snapshot — only sent when real FDM adapter is not publishing.
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py — added FdmState import,
  _on_fdm_state callback, fdm_state stub guard. Frontend field mapping unchanged
  (lat, lon, alt_ft_msl, ias_kt, gnd_speed_kt, hdg_mag_deg, track_deg, vs_fpm, pitch_deg, roll_deg)

## 2026-03-09 — 18:00:00 - Claude Code
- DECIDED: Default FDM initial conditions changed from KJFK to EBBR rwy 25L
- REASON: project targets European operations; EBBR (50.9014°N, 4.4844°E, elev 184ft,
  hdg 254°) is the standard test position. JSBSim was publishing correct data but at
  KJFK coordinates, which looked wrong to users expecting EBBR.
- AFFECTS: src/core/flight_model_adapter/src/JSBSimAdapter.cpp — default IC in initialize()

- DECIDED: Added diagnostic logging to flight_model_adapter_node
- REASON: no visibility into FDM initialization — couldn't tell if model loaded,
  what root dir was used, or if step() was failing. Added RCLCPP_INFO for config,
  root dir, model load success, and RCLCPP_WARN_THROTTLE for step() failures.
- AFFECTS: src/core/flight_model_adapter/src/flight_model_adapter_node.cpp

## 2026-03-09 — 18:30:00 - Claude Code
- DECIDED: Fixed sim_manager aircraft config search path — was missing /config/ subdirectory
- REASON: sim_manager searched for `<aircraft_id>/config.yaml` but actual path is
  `<aircraft_id>/config/config.yaml`. Config never loaded, so current_ic_ stayed all-zeros.
  On RESET, broadcast_ic() published lat=0/lon=0 → JSBSim placed aircraft at 0°/0° (equator).
  Added `/config/config.yaml` variants to search path list (checked before flat `config.yaml`).
- AFFECTS: src/core/sim_manager/src/sim_manager_node.cpp — load_aircraft_config() search paths

- DECIDED: C172 default IC changed from EHAM to EBBR rwy 25L
- REASON: consistent with JSBSim adapter default IC; EBBR is the standard test position
- AFFECTS: src/aircraft/c172/config/config.yaml — lat 50.9014°N, lon 4.4844°E, alt 56m, hdg 254°

- DECIDED: broadcast_ic() now logs IC values (lat/lon in degrees, alt, hdg, config)
- REASON: no visibility into what IC was published on reset; helps diagnose position jumps
- AFFECTS: src/core/sim_manager/src/sim_manager_node.cpp — broadcast_ic()

## 2026-03-09 — 18:15:00 - Claude Code

- DECIDED: Added simple PID autopilot to JSBSimAdapter for pipeline testing (heading hold, altitude hold, airspeed hold)
- REASON: C172P JSBSim model has no built-in autopilot; needed autonomous flight for testing the data pipeline end-to-end
- AFFECTS: JSBSimAdapter.hpp (AutopilotState struct, enable/disable/run_autopilot methods), JSBSimAdapter.cpp (PID controllers writing directly to fcs/aileron-cmd-norm, fcs/elevator-cmd-norm, fcs/throttle-cmd-norm)

- DECIDED: JSBSim engine start uses propulsion/set-running (not propulsion/engine[0]/set-running)
- REASON: propulsion/set-running calls FGPropulsion::InitRunning() which sets magnetos=3 and properly initializes the piston engine; per-engine set-running only sets the Running bool without magnetos → engine dies immediately
- AFFECTS: JSBSimAdapter.cpp — all IC configurations (airborne_clean, ready_for_takeoff)

- DECIDED: Default IC changed to airborne_clean at 2500ft, 90kt, heading 040° with autopilot auto-engaged
- REASON: standalone flight_model_adapter testing needs the aircraft flying immediately; sim_manager will override via apply_initial_conditions()
- AFFECTS: JSBSimAdapter.cpp initialize(), aircraft/c172/config/config.yaml

- DECIDED: airborne_clean IC configuration auto-enables the simple autopilot to hold the IC heading/altitude/airspeed
- REASON: aircraft would diverge immediately without autopilot; ensures stable flight for pipeline testing
- AFFECTS: JSBSimAdapter.cpp apply_initial_conditions() — calls enable_autopilot() for airborne_clean config

## 2026-03-09 — 19:45:00 - Claude Code

- DECIDED: flight_model_adapter subscribes to /sim/state and only steps JSBSim when RUNNING (or standalone with no sim_manager)
- REASON: FREEZE/RUN/RESET state was not propagated to JSBSim — FDM kept advancing during FROZEN, causing lat/lon to update
- AFFECTS: flight_model_adapter_node.cpp — sim_state_sub_, update timer checks sim_state_ before calling step(); publishes is_frozen flag on FdmState

- DECIDED: Map track is unlimited length, cleared on flight reset (RESETTING/READY state transition) or CLR TRACK button
- REASON: previous 500-point cap lost track history during longer flights; track should persist for full session
- AFFECTS: ios/frontend/src/store/useSimStore.js — appendTrackPoint() no longer caps, sim_state handler clears track on reset

- DECIDED: Map aircraft icon is type-aware — fixed-wing silhouette vs helicopter silhouette based on FdmState.is_helicopter
- REASON: framework supports both fixed-wing and rotary-wing; map should visually distinguish them
- AFFECTS: ios_backend_node.py (forwards is_helicopter in fdm_state WS message), useSimStore.js (isHelicopter in fdm state), MapView.jsx (createAircraftIcon renders different SVG per type)

- DECIDED: Aircraft icon color: blue (#2563eb) when RUNNING, red (#ef4444) when FROZEN, gray when other states
- REASON: clear visual feedback of sim state directly on the map; red for frozen is intuitive "stopped" indicator
- AFFECTS: ios/frontend/src/components/MapView.jsx — createAircraftIcon color logic

## 2026-03-09 — 20:30:00 - Claude Code

- DECIDED: Renamed sim_engines → sim_engine_systems throughout workspace
- REASON: user requested rename
- AFFECTS: src/systems/engines/ → src/systems/engine_systems/ (directory), CMakeLists.txt project name, package.xml name, engines_node.cpp node name and log strings, launch/sim_full.launch.py package/name, ios_backend KNOWN_SIM_NODES, aircraft configs (c172, ec135) required_nodes, CLAUDE.md topic table

## 2026-03-09 — 21:00:00 - Claude Code

- DECIDED: Wrapped standalone radionav C++ code as ROS2 lifecycle node ("radionav") at src/core/radionav/
- REASON: integrates the existing radio navigation simulator (VOR, ILS/LOC/GS, DME, NDB, markers with terrain LOS) into the framework as a standard lifecycle node
- AFFECTS: new package.xml, CMakeLists.txt, src/radionav_node.cpp; core nav logic compiled as static library (radionav_core), node links against it; launch/sim_full.launch.py; ios_backend KNOWN_SIM_NODES

- DECIDED: radionav subscribes to /sim/fdm/state (position/altitude) and /sim/controls/avionics (tuned frequencies, OBS), publishes /sim/world/nav_signals (NavSignalTable.msg) at 10 Hz
- REASON: follows CLAUDE.md architecture — world environment topics under /sim/world/, tuned frequencies from arbitrated avionics controls
- AFFECTS: radionav_node.cpp subscriptions and publisher; uses existing NavSignalTable.msg (already defined in sim_msgs)

- DECIDED: radionav data files (earth_nav.dat, WMM.COF, euramec.pc, srtm3/ terrain tiles) installed to share/radionav/data/ via CMakeLists.txt install rule
- REASON: data files need to be findable at runtime; install to share dir with fallback to source tree paths for development
- AFFECTS: CMakeLists.txt install(DIRECTORY data/ ...), radionav_node.cpp path resolution logic

- DECIDED: NavSignalTable.msg retained as-is (already had correct fields for VOR/LOC/GS/DME/NDB/markers)
- REASON: the existing preliminary message definition already matched the radionav RadioResult output; no changes needed
- AFFECTS: no message changes; radionav_node.cpp maps RadioResult fields to NavSignalTable fields

## 2026-03-09 — 22:12:14 - Claude Code
- DECIDED: Removed KNOWN_SIM_NODES hardcoded list from ios_backend_node.py
- REASON: Hardcoded node list violates "aircraft config drives everything" principle and requires IOS changes whenever a node is added/removed
- AFFECTS: ios_backend — send_node_health() now reports all nodes discovered dynamically via heartbeats, lifecycle_state messages, and ROS2 graph queries. No node list to maintain.

## 2026-03-09 — 22:20:31 - Claude Code
- DECIDED: Renamed radionav package to navaid_sim throughout workspace
- REASON: Clearer name; navaid_sim is now a core framework package (no longer external)
- AFFECTS: directory src/core/radionav/ → src/core/navaid_sim/; package name, node name, executable name all changed to navaid_sim / navaid_sim_node; launch file updated; CLAUDE.md updated (nav_sim references → navaid_sim, marked as core package, resolved open decision for NavSignalTable interface)

## 2026-03-09 — 22:37:29 - Claude Code
- DECIDED: Replaced NavState.msg with NavigationState.msg — comprehensive onboard receiver output message
- REASON: NavState.msg was a placeholder with minimal fields; NavigationState.msg covers GPS, NAV1/NAV2 (VOR/ILS/LOC with CDI dots, TO/FROM flags, GS dots), ADF (relative bearing), DME, marker beacons, transponder
- AFFECTS: sim_msgs/msg/NavigationState.msg (new), sim_msgs/msg/NavState.msg (superseded), sim_msgs/CMakeLists.txt, CLAUDE.md topic table (/sim/navigation/state)

- DECIDED: Reimplemented navigation_node without pluginlib — avionics receiver logic is aircraft-agnostic
- REASON: VOR/ILS/ADF/GPS/DME instrument behavior is standard across aircraft types; no need for per-aircraft plugins
- AFFECTS: systems/navigation/src/navigation_node.cpp (full rewrite), CMakeLists.txt, package.xml (removed pluginlib, sim_interfaces, ament_index_cpp deps); launch file removed aircraft_id param

- DECIDED: navigation_node subscribes to /sim/fdm/state (GPS), /sim/world/nav_signals (navaid_sim output), /sim/controls/avionics (OBS, frequencies, transponder) and publishes /sim/navigation/state at 10 Hz
- REASON: Separation of concerns — navaid_sim computes RF environment (signal strength, raw bearing/deviation), navigation_node computes cockpit instrument outputs (CDI dots relative to OBS, TO/FROM flags, relative ADF bearing)
- AFFECTS: navigation_node.cpp subscriptions and publisher; data flow: navaid_sim → /sim/world/nav_signals → navigation_node → /sim/navigation/state

- DECIDED: VOR CDI computed as (OBS - radial) / 4.0 degrees per dot, clamped ±2.5 dots; LOC CDI uses deviation_dots from navaid_sim directly (clamped ±2.5); GS dots passed through clamped ±2.5
- REASON: Matches real VOR full-scale deflection of ±10° (2.5 dots × 4°/dot); LOC deviation already computed by navaid_sim localizer receiver model
- AFFECTS: navigation_node.cpp compute_vor_cdi(), process_nav_receiver()

- DECIDED: GPS output derived directly from FdmState (lat, lon, alt, ground speed, track) — always valid for now, RAIM deferred
- REASON: Simple and correct for initial implementation; GPS equipment modeling (acquisition, RAIM, degradation) is a future enhancement
- AFFECTS: navigation_node.cpp on_update() GPS section

## 2026-03-09 — 22:39:46 - Claude Code
- DECIDED: Added dme_source field to AvionicsControls.msg — 3-position selector: NAV1(0) / HOLD(1) / NAV2(2)
- REASON: C172 KN 64 DME has a physical NAV1/HOLD/NAV2 switch; navigation_node needs to know which NAV receiver to pair DME with, and HOLD freezes the last valid reading
- AFFECTS: sim_msgs/msg/AvionicsControls.msg (new field + constants), sim_msgs/msg/NavigationState.msg (dme_source echo), navigation_node.cpp DME section (source-aware logic with hold state)

## 2026-03-09 — 22:57:58 - Claude Code
- DECIDED: Wired /sim/navigation/state to IOS WebSocket bridge as 'nav_state' message type
- REASON: IOS frontend needs nav receiver data to display GPS, VOR/ILS CDI, ADF, DME, markers, and transponder status
- AFFECTS: ios_backend_node.py (new NavigationState subscription + _on_nav_state handler), useSimStore.js (new 'nav' state object + 'nav_state' WS message handler)

- DECIDED: Added full NAV section to AircraftPanel.jsx below fuel tanks — GPS, NAV1/NAV2 with CDI bars, ADF, DME, markers, transponder
- REASON: Instructor needs to see onboard navigation receiver state in the IOS A/C panel
- AFFECTS: ios/frontend/src/components/panels/AircraftPanel.jsx (new sections: GPS lat/lon/trk/gs/alt, NAV1/NAV2 with CDI deviation bar + GS bar + TO/FROM flags, ADF relative bearing, DME source/distance/gs, marker beacon lights, XPDR code/mode)

## 2026-03-09 — 23:17:56 - Claude Code
- DECIDED: ios_backend subscribes to /sim/controls/avionics (AvionicsControls.msg) and forwards real tuned frequencies to WS clients; avionics stub is suppressed when real data arrives
- REASON: Stub avionics broadcast was overriding real frequencies from the arbitrator, making frequency tuning invisible in the IOS
- AFFECTS: ios_backend_node.py (new AvionicsControls subscription + _on_avionics_controls handler, avionics stub now conditional on 'avionics' not in snapshot)

- DECIDED: ios_backend publishes RawAvionicsControls to /devices/virtual/controls/avionics when it receives 'set_avionics' WS message from frontend
- REASON: IOS needs to act as a virtual avionics head for testing frequency tuning until real hardware is connected
- AFFECTS: ios_backend_node.py (new RawAvionicsControls publisher + publish_avionics method + WS 'set_avionics' handler)

- DECIDED: Added RADIO TUNING section to AircraftPanel.jsx with number inputs for COM1, COM2, NAV1, NAV2 (MHz), ADF (kHz), OBS1, OBS2 (degrees)
- REASON: IOS had no way to tune frequencies; these inputs send set_avionics WS messages to ios_backend which publishes to /devices/virtual/controls/avionics for the input_arbitrator to pick up
- AFFECTS: AircraftPanel.jsx (new FreqInput component + RADIO TUNING section), useSimStore.js (new sendAvionics action, avionics state expanded with obs1/obs2/dmeSource)

## 2026-03-09 — 23:30:15 - Claude Code
- DECIDED: Added diagnostic logging to navaid_sim: BUB VOR lookup on startup, nearest VOR on first position update, frequency-change log with candidate counts
- REASON: Debugging nav1_valid=false required tracing the chain from frequency input → database lookup → candidate filtering; root cause was wrong frequency (BUB=114.60 MHz in earth_nav.dat, user tuned 113.80)
- AFFECTS: navaid_sim_node.cpp (on_configure: BUB lookup log; on_update: first-position nearest VOR log + freq-change candidate log), World.h (new findNearestVOR + findVORByIdent methods)

## 2026-03-09 — 23:38:49 - Claude Code
- DECIDED: Fixed WorldParser::parseXP12 to handle XP810 format (earth_nav.dat 810 Version)
- REASON: Parser only loaded DMEs (5460); VOR/NDB/LOC/GS/Marker all returned 0. Two bugs: (1) line character checks (`line[0]==' ' && line[1]=='3'`) failed because XP810 has no leading space before single-digit type codes; (2) token count requirements were too high (VOR needed >=11 but XP810 has 10 tokens, LOC/GS/Marker needed >=12 but XP810 has 11)
- AFFECTS: WorldParser.cpp parseXP12() — now uses `tokens[0]` for type detection (handles both leading-space and no-leading-space formats), auto-detects XP810 vs XP12 column layout from token count, reduced minimum token requirements. After fix: 3679 VOR, 7315 NDB, 3401 LOC, 3083 GS, 5587 DME, 3356 markers loaded successfully

## 2026-03-10 — Claude Code
- DECIDED: sim_electrical fully wired as lifecycle node with plugin-based solver, subscriptions, and ROS2 publishing
- REASON: electrical system was scaffolded but not functional — only called model->update() without subscribing to inputs or publishing state
- AFFECTS: electrical_node.cpp, IElectricalModel interface (added command_switch, set_engine_n2, get_snapshot), ElectricalState.msg (expanded with per-bus/source/load arrays + summary fields), aircraft_c172 + aircraft_ec135 electrical_model.cpp (implement new interface methods), ios_backend (subscribes /sim/electrical/state, forwards as WS), useSimStore.js (electrical state + sendPanel), AircraftPanel.jsx (bus voltages, source status, switch toggles)
- DECIDED: IElectricalModel::get_snapshot() returns ElectricalSnapshot struct defined in interface header — keeps solver types out of the ROS2 node
- REASON: node should not depend on elec_sys internals; aircraft plugin maps solver state to generic snapshot
- AFFECTS: sim_interfaces/i_electrical_model.hpp, all aircraft electrical_model.cpp implementations
- DECIDED: Switch commands flow: IOS frontend → WS set_panel → ios_backend publish_panel → /devices/virtual/panel → input_arbitrator → /sim/controls/panel → sim_electrical → solver.commandSwitch()
- REASON: follows existing input arbitration pattern — IOS never writes to /sim/ topics directly
- AFFECTS: ios_backend_node.py (set_panel handler), PanelControls.msg topic path

## 2026-03-10 — Claude Code
- DECIDED: IOS panel commands publish to /devices/instructor/panel (INSTRUCTOR priority), not /sim/controls/panel
- REASON: IOS is the instructor station; input_arbitrator already subscribes to /devices/instructor/panel at highest priority and forwards to /sim/controls/panel. Systems nodes must never subscribe to /devices/ topics.
- AFFECTS: ios_backend_node.py (_panel_pub topic), input_arbitrator (already correct — subscribes to all 3 /devices/{hw,virt,inst}/panel)
- DECIDED: Virtual cockpit pages publish to /devices/virtual/panel (VIRTUAL priority), separate from IOS instructor commands
- REASON: virtual cockpit simulates physical switches — lower priority than instructor override. If instructor forces a switch, the virtual cockpit can't override it.
- AFFECTS: ios_backend_node.py (added _virtual_panel_pub + set_virtual_panel WS handler), cockpit pages
- DECIDED: IOS A/C page switches styled as amber FORCE controls, distinct from virtual cockpit green switches
- REASON: visual distinction — instructor sees amber "FORCE" label to indicate override authority, virtual cockpit shows standard switch appearance
- AFFECTS: AircraftPanel.jsx (ForceToggle component), cockpit/CockpitElectrical.jsx (standard ToggleSwitch)
- DECIDED: URL routing via React Router — / for IOS app, /cockpit/c172/* for virtual cockpit panels
- REASON: virtual cockpit panels are separate browser windows (touch screens, secondary monitors); URL routing lets each display show the right panel without tab navigation
- AFFECTS: main.jsx (BrowserRouter + Routes), cockpit/CockpitElectrical.jsx, cockpit/CockpitAvionics.jsx

## 2026-03-10 — Claude Code
- DECIDED: IOS A/C page switches are instructor-level by default — no separate "Force" button needed
- REASON: The act of the instructor touching a switch in the IOS IS the force. The distinction is built into which topic it publishes to (`/devices/instructor/panel`), not a UI button. Three priority tiers: IOS → INSTRUCTOR (always wins), virtual cockpit → VIRTUAL (hardware overrides), hardware MCU → HARDWARE (middle tier). Amber styling on IOS switches visually communicates instructor authority without extra UI chrome.
- AFFECTS: AircraftPanel.jsx (amber ForceToggle, publishes via `set_panel` → `/devices/instructor/panel`), CockpitElectrical.jsx (green ToggleSwitch, publishes via `set_virtual_panel` → `/devices/virtual/panel`)
- DECIDED: All panel UIs (IOS, virtual cockpit, future hardware) read displayed state from `/sim/controls/panel` — the arbitrated output — so they always show what's actually active regardless of source
- REASON: single source of truth; avoids stale or conflicting state displays across panels
- AFFECTS: useSimStore.js (electrical state from arbitrated output), AircraftPanel.jsx, CockpitElectrical.jsx

## 2026-03-10 — Claude Code
- DECIDED: Robust YAML loading pattern for all system nodes that load aircraft config: try/catch around pluginlib load + YAML parse, file-exists check, return FAILURE from on_configure() on error
- REASON: Bad YAML or missing config file should not crash the node or take down the sim. Node stays UNCONFIGURED, operator sees the error in IOS, fixes the file, hits RELOAD, and the node recovers.
- AFFECTS: electrical_node.cpp, fuel_node.cpp, gear_node.cpp (all nodes with pluginlib + YAML config)
- DECIDED: SimAlert topic (`/sim/alerts`) used for config errors — nodes publish SEVERITY_CRITICAL alerts with human-readable error messages
- REASON: Alerts flow through existing ios_backend → WS → frontend pipeline. IOS nodes panel shows last_error detail when node is in unconfigured/error state.
- AFFECTS: SimAlert.msg (already existed), ios_backend_node.py (new subscription + per-node last_error), useSimStore.js (sim_alert handler), NodesPanel.jsx (error detail display)
- DECIDED: Reload logging — on_configure() detects reconfigure (model_ already set) and logs "Reloading <system> config from: <path>" so the operator knows the reload happened
- REASON: Operator feedback during RELOAD workflow — confirms which file was re-read
- AFFECTS: electrical_node.cpp, fuel_node.cpp, gear_node.cpp

## 2026-03-10 — Claude Code
- DECIDED: Extended AvionicsControls.msg with COM3, ADF2, TACAN (channel+band), GPS source selector. Added constants GPS_SOURCE_GPS1/GPS2, TACAN_BAND_X/Y.
- DECIDED: Extended NavigationState.msg with GPS2 (full receiver), ADF2, ILS2 (independent second ILS), TACAN (bearing+distance), COM3 echo, active_gps_source field.
- REASON: Future-proof for glass cockpit aircraft (EC135, multi-GPS), military TACAN support, twin-ADF/twin-ILS configurations. All new fields default to zero/false — no existing node code changes required.
- AFFECTS: sim_msgs/msg/AvionicsControls.msg, sim_msgs/msg/NavigationState.msg. All dependent packages rebuild clean with no code changes.

## 2026-03-10 — Claude Code
- DECIDED: ILS2 removed from NavigationState.msg — NAV2 tuned to an ILS frequency IS the second ILS receiver. A separate ILS2 section was redundant.
- REASON: In real avionics, NAV receivers are VOR/ILS-capable. NAV2 with gs_valid + gs_dots already provides full ILS2 functionality. No separate receiver needed.
- AFFECTS: sim_msgs/msg/NavigationState.msg (ILS2 fields removed), ios_backend_node.py (no ILS2 forwarding needed)
- DECIDED: IOS A/C page is fully dynamic — driven by aircraft navigation.yaml config. Backend loads avionics config via ament_index_python on aircraft_id change, sends avionics_config WS message. Frontend renders radios and nav displays dynamically from config.
- REASON: A/C page must work for any aircraft (C172, EC135, future types) without code changes. Config YAML defines installed equipment; UI renders only what's installed.
- AFFECTS: navigation.yaml (c172, ec135), ios_backend_node.py (_load_avionics_config), useSimStore.js (avionicsConfig state + WS handler), AircraftPanel.jsx (RADIO_FIELD_MAP, DISPLAY_COMPONENTS, dynamic rendering)
- DECIDED: Full GPS2/ADF2/TACAN/COM3 data pipeline wired end-to-end: NavigationState.msg → backend _on_nav_state() → WS nav_state message → Zustand nav store → AircraftPanel display components. Same for AvionicsControls.msg new fields → avionics store.
- REASON: Dynamic A/C page display components reference these fields — they must flow through the entire pipeline or displays show default zeros.
- AFFECTS: ios_backend_node.py (_on_nav_state, _on_avionics_controls), useSimStore.js (nav + avionics initial state + WS handlers)

## 2026-03-10 — 14:30:00 - Claude Code
- DECIDED: Full avionics message audit — standardized all three tiers (RawAvionicsControls, AvionicsControls, NavigationState) for field naming, types, and completeness.
- REASON: Field naming was inconsistent (bare `_freq` vs `_freq_mhz`/`_freq_khz`), ADF1 was missing from AvionicsControls, GPS1 fields used `gps_` prefix instead of `gps1_` to match `gps2_`, NavigationState had float64 where float32 sufficed (except lat/lon).
- AFFECTS: sim_msgs/msg/{RawAvionicsControls,AvionicsControls,NavigationState}.msg, input_arbitrator_node.cpp (to_avionics), navigation_node.cpp (process_nav_receiver float& params, freq echoes, gps1/adf1 renames), navaid_sim_node.cpp (freq field renames), ios_backend_node.py (wire key renames: adf_khz→adf1_khz, adf2_freq_khz→adf2_khz, com3_freq_mhz→com3_mhz, gps_→gps1_, adf_→adf1_), useSimStore.js (store keys: adfKhz→adf1Khz, adf2FreqKhz→adf2Khz, gpsValid→gps1Valid etc), AircraftPanel.jsx (RADIO_FIELD_MAP adf/adf2, GpsDisplay gps→gps1, AdfDisplay adf→adf1)
- DECIDED: Frequency naming convention locked: `_freq_mhz` for MHz, `_freq_khz` for kHz, no bare `_freq` suffix anywhere.
- DECIDED: NavigationState type convention: float64 for lat/lon only, float32 for all other numeric fields, bool for flags, string for idents.
- DECIDED: NavigationState includes frequency echoes (com1/2/3_freq_mhz, adf1/2_freq_khz) from AvionicsControls for IOS display convenience.

## 2026-03-11 — 10:00:00 - Claude Code
- DECIDED: sim_electrical behaviour per sim state — RUNNING: full 50Hz solver with SOC drain. FROZEN: maintain state, keep publishing (IOS needs live data), no SOC drain (dt=0). Switch changes while frozen set solver_dirty flag → solver runs once with dt=0 to reflect new switch state. RESETTING: call model->reset() to reload initial conditions from YAML (battery SOC, switch defaults).
- REASON: Electrical must keep publishing when frozen so IOS panels stay updated. Instructor must be able to flip switches during freeze for scenario setup. Battery SOC must not drain during frozen time. Reset must restore a known good state from aircraft config.
- AFFECTS: electrical_node.cpp (sim_state_ replaces frozen_ bool, solver_dirty_ flag, timer callback rewritten), IElectricalModel interface (reset() added as pure virtual), aircraft_c172/electrical_model.cpp and aircraft_ec135/electrical_model.cpp (reset() implemented via solver_.reset())
- DECIDED: IElectricalModel::reset() added to sim_interfaces — reloads initial conditions (battery SOC, switch defaults) from the topology already loaded by configure(). Does NOT re-read YAML; solver_.reset() uses the already-parsed topology.

## 2026-03-11 — 12:00:00 - Claude Code
- DECIDED: IOS avionics tuning publishes to `/devices/instructor/controls/avionics` (instructor priority), NOT `/devices/virtual/controls/avionics`.
- REASON: The IOS is the instructor station. Its commands must override hardware and virtual inputs. Publishing to virtual meant hardware could override IOS frequency changes — violating the INSTRUCTOR > HARDWARE > VIRTUAL priority chain.
- AFFECTS: ios_backend_node.py (_raw_avionics_pub topic changed, docstring + log message updated)
- DECIDED: IOS topic mapping confirmed consistent across all channels: panel → `/devices/instructor/panel`, avionics → `/devices/instructor/controls/avionics`. Flight and engine instructor publishers not yet implemented (no IOS controls for those yet). `/devices/virtual/` topics reserved for future virtual cockpit pages.
- DECIDED: StatusStrip Row 3 (radios) is now dynamic — driven by `avionicsConfig.radios` from aircraft YAML, same as AircraftPanel. Only installed radios are shown. XPDR always appended.

## 2026-03-11 — 14:00:00 - Claude Code
- DECIDED: Full codebase rename — FDM terminology replaced with Flight Model throughout. "FDM" refers only to aerodynamics/equations of motion; our adapter wraps complete aircraft physics engines (JSBSim, X-Plane, Helisim) that model aero + propulsion + mass + ground reactions. The industry term is "Flight Model".
- REASON: Incorrect terminology was used throughout the codebase. The adapter wraps a complete flight model, not just a flight dynamics model.
- AFFECTS: All 12 source files. Key renames: FdmState.msg → FlightModelState.msg, IFdmAdapter → IFlightModelAdapter (header renamed), /sim/fdm/state → /sim/flight_model/state (topic), fdm_state → flight_model_state (WS type), fdm_name → flight_model_name (msg field), _on_fdm_state → _on_flight_model_state (Python), fdm_state_pub_ → flight_model_state_pub_ (C++), fdm_sub_ → flight_model_sub_ (C++), last_fdm_ → last_flight_model_state_ (C++), fdm_received_ → flight_model_received_ (C++).
- DECIDED: FlightModelCapabilities struct added to IFlightModelAdapter. Reports which subsystems the flight model handles natively (magnetos, mixture, electrical, hydraulic, gear retract, etc.). JSBSimAdapter returns all false (physics only, cockpit systems handled by our nodes). XPlane/Helisim adapters return all true (full native modelling).
- REASON: Systems nodes need to know whether to run their own solver or defer to the flight model's native implementation. Without capabilities, every systems node would need adapter-specific logic.
- AFFECTS: IFlightModelAdapter.hpp (FlightModelCapabilities struct + get_capabilities() pure virtual), JSBSimAdapter.hpp/.cpp (get_capabilities() implemented), flight_model_adapter_node.cpp (can publish capabilities on activate — deferred to when consumers need it).

## 2026-03-12 — 19:00:00 - Claude Code
- DECIDED: sim_engine_systems implemented as lifecycle node with pluginlib — follows exact same pattern as sim_electrical (auto-start timer, on_configure/on_activate/on_deactivate/on_cleanup, 50Hz wall timer, heartbeat, lifecycle_state, SimAlert error reporting).
- REASON: Engine instruments (RPM, EGT, CHT, oil, MAP, N1/N2, TGT, torque) are cockpit systems — they read raw FDM engine physics and apply cockpit logic (switch states, failure overrides, warning thresholds). This separation lets the same FDM serve both piston and turboshaft aircraft with different cockpit behaviour.
- AFFECTS: src/systems/engine_systems/ (new node), src/core/sim_interfaces/ (IEnginesModel + EngineSnapshot + FlightModelCapabilities reused from adapter), src/aircraft/c172/ and src/aircraft/ec135/ (EnginesModel plugins + engine.yaml configs), sim_msgs/msg/EngineState.msg, ios_backend_node.py, useSimStore.js, AircraftPanel.jsx

- DECIDED: IEnginesModel interface uses FlightModelCapabilities + EngineSnapshot structs in sim_interfaces. update() signature: `update(dt, FlightModelState, PanelControls, vector<string> active_failures, FlightModelCapabilities)`. EngineSnapshot uses std::array<float,4> for all per-engine fields (max 4 engines).
- REASON: Matches IElectricalModel pattern. FlightModelCapabilities tells plugin whether FDM models magnetos/mixture/starter natively (X-Plane) or cockpit layer must handle them (JSBSim). Max 4 engines matches FlightModelState arrays.
- AFFECTS: sim_interfaces/i_engines_model.hpp, all engine plugins

- DECIDED: EngineState.msg uses fixed float32[4] and bool[4] arrays, not dynamic arrays. Fields: rpm, egt_degc, cht_degc, oil_pressure_psi, oil_temp_degc, manifold_pressure_inhg, fuel_flow_gph, n1_pct, n2_pct, tgt_degc, torque_pct, engine_running, engine_failed, starter_engaged, low_oil_pressure_warning, high_egt_warning, high_cht_warning, engine_count.
- REASON: Fixed arrays match FlightModelState convention. Covers both piston (RPM, MAP, CHT, EGT) and turboshaft (N1, N2, TGT, torque) in one message. Unused fields zeroed.
- AFFECTS: sim_msgs/msg/EngineState.msg, engines_node.cpp publish_engine_state(), ios_backend, frontend

- DECIDED: FlightModelCapabilities derived from flight_model_name field in FlightModelState message (not from adapter's get_capabilities()). JSBSim/Helisim/custom → cockpit layer handles magnetos/mixture/carb_heat/starter/condition_lever. X-Plane → passthrough (FDM models these natively).
- REASON: Engine systems node needs capabilities but doesn't talk to the adapter directly. flight_model_name is already in FlightModelState — deriving capabilities avoids adding a new topic or service dependency.
- AFFECTS: engines_node.cpp derive_capabilities(), c172 and ec135 plugin update() methods

- DECIDED: engine.yaml as config filename (singular, matching electrical.yaml, fuel.yaml convention). Per-engine panel control IDs defined in YAML (magneto_switch_id, mixture_id, carb_heat_id, starter_id, primer_id for piston; condition_lever_id, starter_id for turboshaft).
- AFFECTS: src/aircraft/c172/config/engine.yaml, src/aircraft/ec135/config/engine.yaml, engines_node.cpp config path, ios_backend _load_engine_config()

- KNOWN GAP (RESOLVED): JSBSim adapter now maps all engine instrument properties — see 2026-03-12 19:30 entry.

## 2026-03-12 — 19:30:00 - Claude Code
- DECIDED: Five new instrument-unit engine fields added to FlightModelState.msg: engine_egt_degc, engine_oil_pressure_psi, engine_oil_temp_degc, engine_cht_degc, engine_manifold_pressure_inhg (all float32[4]).
- REASON: Existing SI fields (itt_k, oil_pressure_pa, oil_temperature_k) required per-frame conversion in every consumer plugin. New fields are adapter-populated in instrument-ready units. SI fields are backfilled for backward compatibility.
- AFFECTS: sim_msgs/msg/FlightModelState.msg, JSBSimAdapter.cpp (get_state), aircraft_c172/engines_model.cpp (reads new fields directly), aircraft_ec135/engines_model.cpp (reads new fields directly)

- DECIDED: JSBSim FGPiston property names mapped (verified from JSBSim source FGPiston.cpp):
  - EGT: `propulsion/engine[N]/egt-degF` (°F → °C: (F-32)*5/9)
  - Oil pressure: `propulsion/engine[N]/oil-pressure-psi` (no conversion)
  - Oil temperature: `propulsion/engine[N]/oil-temperature-degF` (°F → °C)
  - CHT: `propulsion/engine[N]/cht-degF` (°F → °C)
  - MAP: `propulsion/engine[N]/map-inhg` (no conversion)
- REASON: User-provided property names (egt-degR, oil-temperature-degK, cylinder-head-temp-degF, manifest-pressure-inhg) were for FGTurbine, not FGPiston. Verified correct names from JSBSim source PropertyManager->Tie() bindings.
- AFFECTS: JSBSimAdapter.cpp only — property names are adapter-internal, no external interface change

## 2026-03-12 - Design Intent (Chat)
- DECIDED: IC panel fuel tank input uses a slider with dual-unit display: kg AND lbs shown simultaneously on the same control. Slider operates in kg internally (single source of truth). Both values update live as the slider moves. No toggle needed — both are always visible.
- REASON: Instructors may think in either unit depending on background (EASA kg vs FAA lbs). Showing both eliminates mental conversion and prevents loading errors. The real aircraft weight and balance tools (e.g. Jeppesen LoadPlanner) show both units side by side for the same reason.
- DECIDED: Liters and US gallons are NOT shown on the fuel slider — volume is fuel-type and temperature dependent, making it ambiguous as an input. Volume is shown read-only on the fuel state display (from FuelState.tank_quantity_liters) but mass is the authoritative input unit for the IC panel.
- AFFECTS: IC panel (not yet built) — FuelTankSlider component must show dual kg/lbs readout. No backend changes needed; all fuel quantities stored and transmitted in kg throughout.

## 2026-03-12 — 20:30:00 - Claude Code
- DECIDED: FuelState.msg extended with config/display fields: tank_count (uint8), density_kg_per_liter (float32), fuel_type (string), tank_quantity_liters (float32[4]), total_fuel_liters (float32), engine_fuel_flow_lph (float32[4]).
- REASON: Frontend needs tank count to slice fixed [4] arrays, density for kg→L conversion, and fuel type for display. Computing derived volume fields in the node (not frontend) keeps the single-source-of-truth principle.
- AFFECTS: sim_msgs/msg/FuelState.msg, fuel_node.cpp (overlay_config_fields reads fuel.yaml, computes liters and L/h), ios_backend (slices arrays by tank_count, sends fuel_config WS message), useSimStore.js, AircraftPanel.jsx

- DECIDED: fuel.yaml extended with fuel_type, density_kg_per_liter, display_unit, and per-tank name fields.
- REASON: fuel_node.cpp needs density and type to overlay derived fields. ios_backend sends fuel_config to frontend with tank names and capacities for dynamic rendering.
- AFFECTS: src/aircraft/c172/config/fuel.yaml (AVGAS_100LL, 0.72 kg/L), src/aircraft/ec135/config/fuel.yaml (JET_A1, 0.800 kg/L)

- DECIDED: Fuel flow single source of truth — all fuel flow display comes from FuelState.engine_fuel_flow_lph only. Removed engines.fuelFlowGph from AircraftPanel.jsx engine gauges.
- REASON: Two fuel flow sources (EngineState and FuelState) caused oscillation/bouncing in the display. FuelState owns fuel flow because it's a fuel system concern (flow rate per tank feed line).
- AFFECTS: AircraftPanel.jsx (PistonEngineDisplay, TurboshaftEngineDisplay use fuel.engineFuelFlowLph), useSimStore.js (fuel state includes engineFuelFlowLph)

- DECIDED: fuel_node.cpp links yaml-cpp directly (find_package + target_link_libraries) to read fuel.yaml config fields. The IFuelModel plugin interface is NOT changed — config overlay is done in the node after model_->get_state().
- REASON: Plugin interface stays minimal (configure/update/get_state/apply_initial_conditions/reset). Display-only derived fields (liters, L/h, fuel_type) are node concerns, not model concerns.
- AFFECTS: src/systems/fuel/CMakeLists.txt (yaml-cpp linkage)

- PENDING: kg is the single internal truth for fuel mass. Volume (liters) is ONLY a derived display value = mass_kg / density_kg_per_liter. The IC panel fuel input should accept kg (with dual kg/lbs readout per earlier design note). Never store or transmit liters as an authoritative value internally.

## 2026-03-12 - Design Intent (Chat)
- DECIDED: Before adding a third aircraft, sim_fuel must be redesigned around a graph topology model — the same architectural pattern as sim_electrical. The current flat array approach is adequate for C172 and EC135 but will break on any aircraft with crossfeed valves, transfer pumps, collector tanks, or more than 4 tanks.
- REASON: Validation aircraft identified to stress-test system simulation coverage before architecture is locked:
    DA42 Twin Star — 2 engines, 4 tanks (2 main + 2 aux), crossfeed valve, electric boost pumps per side. Tests: multi-engine fuel management, crossfeed, independent boost pumps per engine.
    Cessna C208 Caravan — single engine, belly main tank + optional locker tanks feeding via transfer. Tests: tank transfer logic, optional equipment config.
    Airbus A320 — 5 tanks (L wing, R wing, centre, L outer, R outer), 6 pumps, crossfeed valve, APU feed line, auto transfer from centre/outers. Tests: full graph topology, multi-pump redundancy, auto transfer logic, manifold pressure modelling.
- DECIDED: Target architecture for sim_fuel graph redesign:
    Topology defined in fuel.yaml as named nodes: tanks, pumps, valves, feed lines, engine inlets.
    Edges define flow paths with direction and pressure.
    Solver computes flow through graph at each timestep — same solver/node separation pattern as electrical_solver.hpp.
    FuelState.msg tanks become a named list (not fixed array), or increase fixed array to 8 to cover A320 without breaking existing message consumers.
    float32[4] hard limit raised to float32[8] as minimum viable fix; full dynamic list as preferred redesign.
- DECIDED: Do NOT redesign sim_fuel now. Current implementation is correct for C172 and EC135. Redesign is triggered when the third aircraft (DA42 recommended as first step) is added. By then the graph pattern will be proven by electrical and the scope will be well understood.
- AFFECTS: sim_fuel (future redesign), FuelState.msg (array size or dynamic), fuel.yaml schema (topology graph), IOS fuel panel (must handle variable tank count and named topology).

## 2026-03-12 - Design Intent (Chat) — Engine Type Architecture
- DECIDED: Extend engine system architecture NOW to cover all engine types before more aircraft are added. Reason: most changes are additive (new msg fields, new YAML keys, new capability flags) and backward compatible. The one genuinely breaking architectural issue — turboprop/FADEC write-back — must be decided now before the one-directional data flow assumption becomes load-bearing.

ENGINE TYPE MATRIX — required interfaces per type:

  piston (C172):
    Inputs from panel: sel_magnetos, pot_mixture, sw_carb_heat, btn_starter, btn_primer
    FlightModel outputs used: rpm[N], egt_degc[N], cht_degc[N], oil_pressure_psi[N], oil_temp_degc[N], manifold_pressure_inhg[N], fuel_flow_lph[N]
    EngineState outputs: rpm, egt, cht, oil_pressure, oil_temp, mp, fuel_flow, engine_running, warnings
    Write-back to FlightModel: none (JSBSim models piston physics)

  turbocharged_piston (DA42 diesel / turbocharged Lycoming):
    Adds vs piston: boost_pressure_inhg, intercooler_temp_degc, turbo_inlet_temp_degc
    Removes vs piston: carb_heat (not applicable), magnetos if FADEC (DA42 uses FADEC)
    FADEC variant: no magneto/mixture logic — cockpit sends power lever angle only
    Write-back: none for JSBSim; FADEC variant may need fuel_flow_command if JSBSim doesnt model it

  turboshaft (EC135):
    Inputs from panel: sel_condition[N], btn_starter[N]
    FlightModel outputs used: n1_pct[N], n2_pct[N], tgt_degc[N], torque_pct[N], ng_pct[N]
    EngineState outputs: n1, n2, tgt, torque, ng, engine_running, warnings
    Write-back: none

  turboprop (C208, ATR, King Air):
    Inputs from panel: throttle[N], prop_lever[N], condition_lever[N], btn_starter[N]
    FlightModel outputs used: n1_pct[N], itt_degc[N], torque_pct[N], prop_rpm[N], beta_deg[N]
    EngineState adds: prop_rpm[4], beta_deg[4], feather_state[4], reverse_active[4]
    CRITICAL — write-back required: prop_lever position commands propeller pitch in beta range.
      sim_engine_systems must publish /sim/engines/commands topic.
      IFlightModelAdapter must subscribe to /sim/engines/commands and apply prop pitch.
      This is the ONLY engine type requiring write-back for JSBSim (X-Plane handles it natively).
    FlightModelCapabilities adds: models_prop_governor, models_beta_range, models_reverse_thrust

  turbojet (older jets, Learjet 23):
    Inputs from panel: thrust_lever[N], btn_starter[N], sw_ignition[N]
    FlightModel outputs used: n1_pct[N], n2_pct[N], egt_degc[N], epr[N], fuel_flow_lph[N]
    EngineState adds: epr[4]
    Write-back: none (JSBSim models turbojet physics)

  turbofan_fadec (A320, modern jets):
    Inputs from panel: tla_deg[N] (thrust lever angle), toga_btn[N], flex_temp_degc
    FlightModel outputs used: n1_pct[N], n2_pct[N], egt_degc[N], epr[N], fuel_flow_lph[N], oil_pressure[N], oil_temp[N], vibration[N]
    EngineState adds: epr[4], vibration_level[4], fadec_mode[4] (string: NORM/TOGA/FLEX/IDLE/REV)
    Write-back: if FlightModelCapabilities.models_fadec = false, sim_engine_systems must compute
      N1 target and publish /sim/engines/commands. If true (X-Plane), passthrough only.
    FlightModelCapabilities adds: models_fadec, models_autothrust

WRITE-BACK ARCHITECTURE (DECIDED):
  sim_engine_systems publishes /sim/engines/commands (EngineCommands.msg) containing:
    float32[4] prop_pitch_deg       # turboprop beta range command
    float32[4] n1_target_pct       # FADEC N1 target when modelled in sim_engine_systems
    float32[4] fuel_flow_command_kgs # FADEC fuel flow when modelled here
    bool[4]    feather_command
    bool[4]    reverse_command
    uint8      engine_count
  IFlightModelAdapter subscribes to /sim/engines/commands and applies them in step().
  JSBSimAdapter applies via property tree writes (propulsion/engine[N]/blade-angle etc).
  XPlaneAdapter ignores (X-Plane FADEC handles it natively — capability flags are all true).
  This topic is ALWAYS published by sim_engine_systems. For piston/turboshaft it carries zeros — consumers ignore unused fields. No conditional publishing.

NEW FIELDS TO ADD NOW (additive, no breaks):
  FlightModelCapabilities: models_prop_governor, models_beta_range, models_reverse_thrust, models_fadec, models_autothrust (all default false)
  EngineState.msg: epr[4], prop_rpm[4], beta_deg[4], feather_state[4], reverse_active[4], vibration_level[4], fadec_mode[4] (string)
  engine.yaml: engine_type field extended to: piston | turbocharged_piston | fadec_piston | turboshaft | turboprop | turbojet | turbofan_fadec
  New msg: EngineCommands.msg (see above)
  IFlightModelAdapter: subscribe to /sim/engines/commands, apply in step()

WHAT DOES NOT CHANGE:
  IEnginesModel::update() signature unchanged — PanelControls already carries all lever positions generically.
  Existing C172 piston plugin and EC135 turboshaft plugin unchanged.
  All existing topics unchanged.
  EngineCommands published as zeros for piston/turboshaft — no behaviour change.

- AFFECTS: sim_msgs (EngineState.msg extended, EngineCommands.msg new), IFlightModelAdapter (new subscription), sim_engine_systems (publish EngineCommands), JSBSimAdapter (apply commands), engine.yacmalt  s>c>h e~m/as,i mFulliagthotrM_ofdrealmCeawpoarbki/lDiEtCiIeSsI OsNtSr.umcdt .<
<E O'FE
OeFc'h
o
 #"#w r2i0t2t6e-n0"3
-12 - Design Intent (Chat) — Engine Type Architecture
- DECIDED: Extend engine system architecture NOW to cover all engine types before more aircraft are added. Reason: most changes are additive (new msg fields, new YAML keys, new capability flags) and backward compatible. The one genuinely breaking architectural issue — turboprop/FADEC write-back — must be decided now before the one-directional data flow assumption becomes load-bearing.

ENGINE TYPE MATRIX — required interfaces per type:

  piston (C172):
    Inputs from panel: sel_magnetos, pot_mixture, sw_carb_heat, btn_starter, btn_primer
    FlightModel outputs used: rpm[N], egt_degc[N], cht_degc[N], oil_pressure_psi[N], oil_temp_degc[N], manifold_pressure_inhg[N], fuel_flow_lph[N]
    EngineState outputs: rpm, egt, cht, oil_pressure, oil_temp, mp, fuel_flow, engine_running, warnings
    Write-back to FlightModel: none (JSBSim models piston physics)

  turbocharged_piston (DA42 diesel / turbocharged Lycoming):
    Adds vs piston: boost_pressure_inhg, intercooler_temp_degc, turbo_inlet_temp_degc
    Removes vs piston: carb_heat (not applicable), magnetos if FADEC (DA42 uses FADEC)
    FADEC variant: no magneto/mixture logic — cockpit sends power lever angle only
    Write-back: none for JSBSim; FADEC variant may need fuel_flow_command if JSBSim doesnt model it

  turboshaft (EC135):
    Inputs from panel: sel_condition[N], btn_starter[N]
    FlightModel outputs used: n1_pct[N], n2_pct[N], tgt_degc[N], torque_pct[N], ng_pct[N]
    EngineState outputs: n1, n2, tgt, torque, ng, engine_running, warnings
    Write-back: none

  turboprop (C208, ATR, King Air):
    Inputs from panel: throttle[N], prop_lever[N], condition_lever[N], btn_starter[N]
    FlightModel outputs used: n1_pct[N], itt_degc[N], torque_pct[N], prop_rpm[N], beta_deg[N]
    EngineState adds: prop_rpm[4], beta_deg[4], feather_state[4], reverse_active[4]
    CRITICAL — write-back required: prop_lever position commands propeller pitch in beta range.
      sim_engine_systems must publish /sim/engines/commands topic.
      IFlightModelAdapter must subscribe to /sim/engines/commands and apply prop pitch.
      This is the ONLY engine type requiring write-back for JSBSim (X-Plane handles it natively).
    FlightModelCapabilities adds: models_prop_governor, models_beta_range, models_reverse_thrust

  turbojet (older jets, Learjet 23):
    Inputs from panel: thrust_lever[N], btn_starter[N], sw_ignition[N]
    FlightModel outputs used: n1_pct[N], n2_pct[N], egt_degc[N], epr[N], fuel_flow_lph[N]
    EngineState adds: epr[4]
    Write-back: none (JSBSim models turbojet physics)

  turbofan_fadec (A320, modern jets):
    Inputs from panel: tla_deg[N] (thrust lever angle), toga_btn[N], flex_temp_degc
    FlightModel outputs used: n1_pct[N], n2_pct[N], egt_degc[N], epr[N], fuel_flow_lph[N], oil_pressure[N], oil_temp[N], vibration[N]
    EngineState adds: epr[4], vibration_level[4], fadec_mode[4] (string: NORM/TOGA/FLEX/IDLE/REV)
    Write-back: if FlightModelCapabilities.models_fadec = false, sim_engine_systems must compute
      N1 target and publish /sim/engines/commands. If true (X-Plane), passthrough only.
    FlightModelCapabilities adds: models_fadec, models_autothrust

WRITE-BACK ARCHITECTURE (DECIDED):
  sim_engine_systems publishes /sim/engines/commands (EngineCommands.msg) containing:
    float32[4] prop_pitch_deg       # turboprop beta range command
    float32[4] n1_target_pct       # FADEC N1 target when modelled in sim_engine_systems
    float32[4] fuel_flow_command_kgs # FADEC fuel flow when modelled here
    bool[4]    feather_command
    bool[4]    reverse_command
    uint8      engine_count
  IFlightModelAdapter subscribes to /sim/engines/commands and applies them in step().
  JSBSimAdapter applies via property tree writes (propulsion/engine[N]/blade-angle etc).
  XPlaneAdapter ignores (X-Plane FADEC handles it natively — capability flags are all true).
  This topic is ALWAYS published by sim_engine_systems. For piston/turboshaft it carries zeros — consumers ignore unused fields. No conditional publishing.

NEW FIELDS TO ADD NOW (additive, no breaks):
  FlightModelCapabilities: models_prop_governor, models_beta_range, models_reverse_thrust, models_fadec, models_autothrust (all default false)
  EngineState.msg: epr[4], prop_rpm[4], beta_deg[4], feather_state[4], reverse_active[4], vibration_level[4], fadec_mode[4] (string)
  engine.yaml: engine_type field extended to: piston | turbocharged_piston | fadec_piston | turboshaft | turboprop | turbojet | turbofan_fadec
  New msg: EngineCommands.msg (see above)
  IFlightModelAdapter: subscribe to /sim/engines/commands, apply in step()

WHAT DOES NOT CHANGE:
  IEnginesModel::update() signature unchanged — PanelControls already carries all lever positions generically.
  Existing C172 piston plugin and EC135 turboshaft plugin unchanged.
  All existing topics unchanged.
  EngineCommands published as zeros for piston/turboshaft — no behaviour change.

- AFFECTS: sim_msgs (EngineState.msg extended, EngineCommands.msg new), IFlightModelAdapter (new subscription), sim_engine_systems (publish EngineCommands), JSBSimAdapter (apply commands), engine.yaml schema, FlightModelCapabilities struct.

## 2026-03-21 — 00:00:00 - Claude Code
- DECIDED: GearState.msg created at src/sim_msgs/msg/GearState.msg and registered in sim_msgs CMakeLists.txt
- REASON: sim_gear node needs a dedicated output message for /sim/gear/state covering both fixed and retractable gear aircraft. Message design mirrors FlightModelState gear arrays (5-leg max, position_pct, weight_on_wheels, status) while adding cockpit-layer concerns: aggregate booleans (on_ground, gear_handle_down, gear_unsafe, gear_warning), brake echoes from FlightControls (brake_left, brake_right, parking_brake), and nosewheel steering angle.
- AFFECTS: src/sim_msgs/msg/GearState.msg (new), src/sim_msgs/CMakeLists.txt (GearState.msg inserted between FuelState.msg and HydraulicState.msg in alphabetical order)

## 2026-03-12 — 21:30:00 - Claude Code
- DECIDED: Implemented engine type architecture future-proofing (all additive, no breaks).
- CHANGES MADE:
  1. EngineState.msg: added epr[4], prop_rpm[4], beta_deg[4], feather_state[4], reverse_active[4], vibration_level[4], fadec_mode[4] (string), boost_pressure_inhg[4], intercooler_temp_degc[4]. All zeroed for piston/turboshaft.
  2. EngineCommands.msg: new message — prop_pitch_deg[4], n1_target_pct[4], fuel_flow_command_kgs[4], feather_command[4], reverse_command[4], engine_count. Published by sim_engine_systems at 50Hz (all zeros for current aircraft).
  3. FlightModelCapabilities: added models_prop_governor, models_beta_range, models_reverse_thrust, models_fadec, models_autothrust (all default false) — in both IFlightModelAdapter.hpp and i_engines_model.hpp.
  4. IFlightModelAdapter: added virtual apply_engine_commands() with default no-op. JSBSimAdapter overrides with no-op stub. flight_model_adapter_node subscribes to /sim/engines/commands.
  5. engine.yaml: added engine_type field — c172="piston", ec135="turboshaft".
  6. engines_node.cpp: reads engine_type from YAML, logs "Engine type: <type>, engine_count: <N>" at startup. Links yaml-cpp.
- REASON: Pre-wire interfaces for turboprop (C208/King Air), turbojet, turbofan_fadec (A320), and turbocharged piston (DA42) before architecture becomes load-bearing. All changes additive — C172 and EC135 plugins completely unchanged.
- AFFECTS: sim_msgs (EngineState.msg, EngineCommands.msg, CMakeLists.txt), IFlightModelAdapter.hpp, JSBSimAdapter.hpp/.cpp, flight_model_adapter_node.cpp, i_engines_model.hpp, engines_node.cpp, engine_systems/CMakeLists.txt, c172/config/engine.yaml, ec135/config/engine.yaml

## 2026-03-12 — 00:00:00 - Claude Code
- DECIDED: Fix EngineState.msg semantic errors before message shape becomes load-bearing.
  1. Added `string engine_type` scalar field alongside `engine_count` — aircraft-level type tag (piston/turboshaft/turboprop/etc).
  2. Replaced `string[4] fadec_mode` with four named scalars: `fadec_mode_0` through `fadec_mode_3`. ROS2 `string[N]` is syntactically valid but behaves as unbounded dynamic strings, not a true fixed-size array — causes serialisation surprises and IDL ambiguity.
  3. engines_node.cpp: `publish_engine_state()` now sets `engine_type` from YAML and initialises all four `fadec_mode_*` fields to "OFF".
- REASON: Design review caught these issues. Fixing now avoids breaking downstream consumers once the message is relied upon by IOS/QTG/recording.
- AFFECTS: sim_msgs/msg/EngineState.msg, systems/engine_systems/src/engines_node.cpp

## 2026-03-12 - Design Intent (Chat) — Fuel Flow Semantics and Graph Solver Scope
- DECIDED: FuelState.engine_fuel_flow_kgs[4] and engine_fuel_flow_lph[4] are PER-ENGINE consumption rates — how fast each engine burner consumes fuel. This is what the pilot reads on the fuel flow gauge and what JSBSim reports.
- NOTE: Per-engine flow is NOT the same as per-tank drain rate. On any aircraft with crossfeed or transfer (DA42, A320), the engine consumes from a manifold — the tanks draining depend on which pumps and valves are open, not directly on which engine is running. The future graph solver will add float32[8] tank_flow_out_lph per tank outlet. For C172 and EC135 this distinction is irrelevant — no crossfeed, each engine feeds its own side.

- DECIDED: Fuel graph solver redesign — do the cheap part now, defer the expensive part:
  CHEAP (doing now): Raise FuelState.msg fixed arrays from [4] to [8]. Additive, zero risk, covers A320 5-tank topology without touching solver logic. All existing consumers still work — they read index 0..engine_count-1 and zeros beyond.
  EXPENSIVE (deferred to DA42): Full graph solver — new topology YAML schema, new fuel_solver.hpp/.cpp library (same pattern as electrical_solver), IFuelModel interface changes, C172 and EC135 plugin rewrites. This is a multi-session Opus task, not a quick fix. Trigger: third aircraft addition.
  REASON: Engine capabilities extension was purely additive (new fields/flags). Fuel graph is a solver rewrite — different category of work. Wrong risk/reward to do it before DA42 forces the design.

- DECIDED: Add float32[8] tank_flow_out_lph to FuelState.msg now (additive) to reserve the field for future graph solver output. Zeroed by current flat solver — consumers can already subscribe to it.

- AFFECTS: FuelState.msg (arrays [4]→[8], new tank_flow_out_lph[8] field), no other changes.

## 2026-03-12 — 00:01:00 - Claude Code
- DECIDED: Implement FuelState.msg changes from fuel flow semantics design review.
  1. Raised all per-tank fixed arrays from [4] to [8]: tank_quantity_kg, tank_quantity_pct, tank_usable_kg, tank_quantity_liters, fuel_pressure_pa, tank_selected. Per-engine arrays (engine_fuel_flow_kgs[4], engine_fuel_flow_lph[4], boost_pump_on[4]) left at [4].
  2. Added `float32[8] tank_flow_out_lph` — zeroed by current flat solver, reserved for future graph solver.
  3. fuel_node.cpp: fixed `overlay_config_fields()` tank loop from `i < 4` to `i < 8` to cover all slots. Zero-fills tank_flow_out_lph in same loop.
- REASON: Covers A320 5-tank topology without touching solver logic. Additive, zero risk — existing consumers read index 0..tank_count-1 and ignore zeros beyond. IOS backend already uses dynamic `range(n)`.
- AFFECTS: sim_msgs/msg/FuelState.msg, systems/fuel/src/fuel_node.cpp

## 2026-03-12 - Design Intent (Chat) — Fuel Graph Solver: Attitude Physics
- DECIDED: The fuel graph solver must model attitude-induced fuel redistribution, not just valve/pump switch states. This is a real-world phenomenon: in a C208 sideslip, lateral acceleration creates differential head pressure between wing tanks, causing the low-wing tank to drain faster. Pilots observe this as fuel imbalance developing on the gauges and must correct with the fuel selector.
- REASON: The flat solver cannot model this — it has no concept of physics acting on the fluid. The graph solver computes flow from pressure differentials, making attitude-induced flow a natural consequence of the physics model.
- DECIDED: Each tank node in the fuel topology YAML must include a physical position in aircraft body frame (meters from datum):
    position_m: {x: -2.8, y: 0.4, z: 0.0}  # lateral, longitudinal, vertical
  The solver uses tank positions + FlightModelState (attitude, accelerations) to compute the instantaneous gravity head pressure at each tank outlet each timestep.
  Formula: head_pressure = density_kg_per_liter * g_effective * h
  Where g_effective is the component of total acceleration acting along the tank-to-outlet axis.
- AFFECTS ALSO:
  Engine-driven pump vs boost pump: at high AoA or unusual attitude, gravity feed may or may not be sufficient without boost pump pressure. Flat solver always delivers fuel — graph solver computes whether flow is physically possible.
  A320 centre tank: pumps must overcome the pressure differential to transfer to wing tanks. Graph solver models this naturally from positions and pump max_flow_lph.
- AFFECTS: fuel.yaml schema (tank position_m required for all tank nodes), fuel graph solver (subscribes to FlightModelState for attitude + accelerations, computes g_effective per tank), C208/DA42/A320 YAML topology authoring.
- NOTE: For C172 and EC135 — symmetric wing tanks, no crossfeed. Attitude-induced imbalance is minor and unmodelled by the flat solver. Acceptable for FNPT II on those types.

## 2026-03-12 - Design Intent (Chat) — JSBSim Fuel Data Ownership, IOS Impact, and Failures Architecture

### CORRECTION to previous attitude physics note:
JSBSim already owns tank position (vXYZ body frame), capacity, contents, unusable fuel, standpipe,
drain location, priority, and inertia contribution (ixx/iyy/izz). We must NOT duplicate this in
our fuel.yaml. Doing so creates two sources of truth that will diverge.

DECIDED: fuel.yaml splits into two concerns:
  1. Physics config — owned by JSBSim aircraft XML. Position, capacity, unusable, drain location.
     JSBSimAdapter reads these from JSBSim property tree and publishes in FuelState.
     Our fuel.yaml does NOT re-specify these.
  2. Cockpit interface config — owned by our fuel.yaml. Human-readable tank names, IOS panel
     layout, switch IDs, pump/valve topology, feed selector positions, display_unit, density.
     These are things JSBSim has no concept of — they are simulator cockpit concerns.

DECIDED: fuel_config WS message (sent to IOS on connect) is built by merging:
  - Tank names and display config from fuel.yaml (cockpit interface)
  - Tank capacities and positions from JSBSim property tree via JSBSimAdapter
  This merge happens in ios_backend. For X-Plane, both come from X-Plane data refs.

### IOS PANEL IMPACT:
Current state: IOS fuel panel shows tank quantities, total fuel, fuel flow. Unaffected by this
  architectural change — it reads FuelState which comes from JSBSim property tree already.
Future state (when graph solver arrives): IOS gains a fuel system schematic view (FUEL tab on
  A/C panel) showing pump and valve states. Instructor can see which pumps are running and
  inject failures directly from the schematic. This requires the graph solver to exist first.
  Until then the IOS fuel panel is display-only with no topology view.

### FAILURE INJECTION ARCHITECTURE:
sim_failures is currently a pure stub (FailureState.msg = bool placeholder).
Failures split into two categories with different injection mechanisms:

CATEGORY 1 — JSBSim property tree failures (implementable now, no graph solver needed):
  fuel_leak_tank_N: sim_failures publishes active failure → JSBSimAdapter applies drain rate
    each step() by writing propulsion/tank[N]/contents-lbs -= leak_rate_lbs_per_sec * dt
  fuel_exhaustion: natural — JSBSim drains automatically, no injection needed
  fuel_starvation_tank_N: write propulsion/tank[N]/contents-lbs = unusable quantity
  These are trivial. JSBSimAdapter's step() checks active_failures list and applies writes.

CATEGORY 2 — Graph solver failures (require graph solver — deferred to DA42):
  boost_pump_failure_N: Currently cosmetic only (warning light, no effect on fuel delivery).
    With graph solver: pump node disabled → engine gets gravity feed pressure only →
    potential starvation at high power or unusual attitude.
  crossfeed_valve_stuck: Not possible without graph solver topology.
  fuel_filter_blocked: Reduces flow rate on affected edge — needs graph solver.
  These failures are TRAINING-CRITICAL — boost pump failure is a standard training scenario.
  Without the graph solver, injecting boost pump failure only lights a warning light.
  This is the strongest argument for the graph solver after DA42.

CATEGORY 3 — Display/IOS layer failures (no backend changes needed):
  fuel_qty_indicator_failed_N: IOS frontend shows wrong value regardless of FuelState.
    Implemented entirely in AircraftPanel.jsx — replace tank value with failed indicator.
  These can be implemented now once sim_failures is real.

CATEGORY 4 — Engine performance failures triggered by fuel state:
  fuel_contamination: sim_failures flag → sim_engine_systems degrades engine output.
  fuel_low_pressure: generated automatically by fuel system when pump fails in graph solver.

DECIDED: sim_failures implementation priority order:
  1. FailureState.msg — replace bool placeholder with string[] active_failure_ids
     (list of active failure IDs e.g. ["fuel_leak_tank_0", "engine_failure_1"])
  2. JSBSimAdapter reads active_failure_ids in step() and applies property tree writes
     for Category 1 failures (fuel leaks, engine failures, control surface failures)
  3. IOS FAIL tab — instructor panel for injecting/clearing failures
  4. Category 3 display failures in frontend
  5. Category 2 graph solver failures — after DA42

- AFFECTS: FailureState.msg (redesign), JSBSimAdapter (failure application in step()),
  sim_failures node (publish active list, subscribe to IOS commands), IOS FAIL tab (new),
  AircraftPanel.jsx (display failure states), fuel graph solver (Category 2, deferred).

## 2026-03-12 — 00:02:00 - Claude Code
- DECIDED: Make JSBSimAdapter fuel section fully dynamic — no hardcoded tank indices or counts.
  1. Tank count read via C++ API: `exec_->GetPropulsion()->GetNumTanks()`, clamped to 4 (FlightModelState.fuel_tank_kg array size).
  2. Per-tank contents and capacity read in a loop using indexed property strings (`propulsion/tank[i]/contents-lbs`, `propulsion/tank[i]/capacity-lbs`).
  3. `fuel_total_kg` computed as sum of all tanks from the loop, not from `propulsion/total-fuel-lbs`.
  4. `fuel_total_pct` computed from total contents / total capacity across all tanks.
  5. Remaining `fuel_tank_kg` slots beyond num_tanks zero-filled explicitly.
  6. Added `#include <models/FGPropulsion.h>` to access `GetNumTanks()`.
  7. Removed "C172 has two tanks" comment — code is now aircraft-agnostic.
- REASON: Previous code hardcoded tank[0] and tank[1], breaking silently for any aircraft with 3+ tanks. JSBSim has no `propulsion/tank-count` property — `GetNumTanks()` is C++ API only.
- AFFECTS: src/core/flight_model_adapter/src/JSBSimAdapter.cpp (fuel section only)

## 2026-03-12 — 00:03:00 - Claude Code
- DECIDED: Add fuel capability flags to FlightModelCapabilities and gate fuel_node data source on them.
  Same pattern as engine capabilities: if the flight model has the capability it models it, if it doesn't we model it.
  1. FlightModelState.msg: added `float32[8] fuel_tank_capacity_kg` — per-tank capacity from flight model.
  2. FlightModelCapabilities (both IFlightModelAdapter.hpp and sim_interfaces/i_engines_model.hpp): added three fuel flags:
     `models_fuel_quantities` (tank contents + capacity from FDM), `models_fuel_pump_pressure`, `models_fuel_crossfeed`.
  3. JSBSimAdapter: sets `models_fuel_quantities = true` (JSBSim owns tank data). Populates `fuel_tank_capacity_kg` in the dynamic tank loop.
  4. fuel_node.cpp: `derive_capabilities()` infers `models_fuel_quantities_` from `flight_model_name` (same pattern as engines_node).
     When true: tank_count derived from non-zero FDM capacity slots, tank_quantity_pct computed from FDM capacity.
     When false: tank_count and capacity from fuel.yaml (unchanged behaviour).
- REASON: Implements the DECISIONS.md "JSBSim Fuel Data Ownership" design — JSBSim is the single source of truth for tank physics when it has the capability. fuel.yaml remains valid for the non-JSBSim path. C172/EC135 behaviour identical (JSBSim values match YAML values).
- AFFECTS: FlightModelState.msg, IFlightModelAdapter.hpp, i_engines_model.hpp, JSBSimAdapter.cpp, fuel_node.cpp

## 2026-03-12 — 22:00:00 - Claude Code
- DECIDED: Embed fuel capability flags directly in FlightModelState.msg instead of deriving from flight_model_name string.
  1. FlightModelState.msg: added `cap_models_fuel_quantities`, `cap_models_fuel_pump_pressure`, `cap_models_fuel_crossfeed` (bool) in metadata section + `fuel_tank_count` (uint8) in fuel section.
  2. flight_model_adapter_node.cpp: caches `adapter_->get_capabilities()` in on_configure(), sets cap_* fields on every published state message.
  3. JSBSimAdapter.cpp: sets `fuel_tank_count` from `GetNumTanks()` in get_state().
  4. fuel_node.cpp: removed `derive_capabilities()` method and `models_fuel_quantities_` member. Now reads `cap_models_fuel_quantities` and `fuel_tank_count` directly from the FlightModelState message.
- REASON: Previous approach derived capabilities by string-matching `flight_model_name` — fragile, breaks for renamed/custom FDMs. Tank count was derived by counting non-zero capacity slots — breaks if a valid tank has capacity=0. The adapter already has `get_capabilities()` but never published its results. Now the data flows through the existing 50Hz FlightModelState message with no new topics or services.
- AFFECTS: sim_msgs/msg/FlightModelState.msg, flight_model_adapter_node.cpp, JSBSimAdapter.cpp, fuel_node.cpp

## 2026-03-12 - Design Intent (Chat) — Capability Distribution: Known Technical Debt
- CURRENT STATE: Both engines_node and fuel_node infer FlightModelCapabilities by calling
  derive_capabilities(msg->flight_model_name) — pattern-matching the FDM name string
  (e.g. "jsbsim" → set flags). This is consistent across both nodes but architecturally fragile.
- PROBLEM: Capability truth is duplicated — once in JSBSimAdapter (where flags are set) and
  again in each system node (where flags are re-derived from a string). Adding a new FDM or
  renaming an adapter requires updating every node's derive_capabilities() function.
- CORRECT PATTERN: JSBSimAdapter should publish FlightModelCapabilities on a dedicated topic
  (e.g. /sim/flight_model/capabilities) once at on_activate(). All system nodes subscribe and
  store the received struct. No string matching anywhere. Capability truth lives in one place.
- DECIDED: Defer this refactor until X-Plane adapter is added. At that point X-Plane needs
  all capability flags = true (full passthrough), which will make the string-matching approach
  messy enough to force the proper fix. The refactor will touch: JSBSimAdapter (publish caps
  topic), XPlaneAdapter (publish caps topic), engines_node (subscribe instead of derive),
  fuel_node (subscribe instead of derive), any future system nodes, launch file.
- RISK until then: Low. Single FDM (JSBSim), consistent pattern across both nodes,
  flight_model_name field is stable.
- AFFECTS (future): IFlightModelAdapter.hpp (publish caps), engines_node.cpp, fuel_node.cpp,
  XPlaneAdapter.cpp, launch/sim_full.launch.py.

## 2026-03-12 — 14:00:00 - Claude Code

### sim_failures system: full implementation

- DECIDED: **FailureState.msg redesigned** — replaced `bool placeholder` with:
  `active_failure_ids[]`, `armed_failure_ids[]`, `armed_trigger_remaining_s[]`,
  `failed_nav_receivers[]`, `failed_instruments[]`. This is the authoritative failure
  status broadcast consumed by IOS FAIL tab.
- REASON: Placeholder was blocking any failure injection work. New fields cover active,
  armed (pending trigger), and internally-handled state (nav receivers, instruments).

- DECIDED: **FailureCommand.msg** — new IOS→sim_failures command message with fields:
  `action` (inject/arm/clear/clear_all), `failure_id`, `trigger_mode` (delay/condition),
  `trigger_delay_s`, `condition_param`, `condition_operator`, `condition_value`,
  `condition_duration_s`.
- REASON: IOS needs a single command interface for all failure operations including
  delayed and condition-based arming (e.g. inject at altitude, after delay).

- DECIDED: **FailureInjection.msg** — new sim_failures→handler routing message with fields:
  `failure_id`, `handler`, `method`, `params_json`, `active`.
- REASON: sim_failures is the central failure authority but doesn't implement handler logic.
  It routes injection commands to handler-specific topics. params_json uses simple std::string
  formatting — no external JSON library dependency.

- DECIDED: **3-topic routing** for FailureInjection:
  `/sim/failure/flight_model_commands` → flight_model_adapter
  `/sim/failure/electrical_commands` → sim_electrical
  `/sim/failure/navaid_commands` → navaid_sim
  Internal handlers (`sim_failures` itself): nav receiver + instrument failures.
- REASON: Topics-not-services — all failure commands are recordable in rosbag2 for QTG
  replay and post-incident analysis. Each handler subscribes only to its own topic,
  keeping coupling minimal.

- DECIDED: **failures.yaml per aircraft** — YAML schema with ATA chapter organisation:
  each entry has `id`, `ata` (chapter number or null for world), `display_name`,
  `category`, and `injection` block (`handler`, `method`, `params` map).
  C172: 18 failures. EC135: 24 failures (twin engine variants, tail rotor, retractable gear).
- REASON: ATA chapter numbering is industry standard for maintenance/training categorisation.
  IOS FAIL tab can group by ATA chapter. Params map allows handler-specific arguments
  without schema coupling.

- DECIDED: **CB 3-state model** — circuit breaker failures use `set_circuit_breaker_state`
  with `state: popped` param, routed to electrical handler. Physical CB state (closed/popped/pulled)
  is managed by ElectricalSolver — sim_failures only triggers the transition.
- REASON: CBs are part of the electrical topology, not standalone failures. The electrical
  solver already handles CB state transitions; sim_failures just commands them.

- DECIDED: **World vs aircraft failures** — `world_navaid_station_failed` (ata: null) routes
  to `navaid_sim` handler, not sim_failures. Station ID is a runtime parameter (empty default
  in YAML, IOS fills it). Aircraft nav receiver failures (`ata34_nav*_receiver_failed`) are
  handled internally by sim_failures via `failed_nav_receivers_` set.
- REASON: Ground station failure affects the signal environment (navaid_sim domain).
  Onboard receiver failure affects what the aircraft can receive (sim_failures domain).
  Clean separation: world environment vs aircraft equipment.

- DECIDED: **Armed queue with condition triggers** — armed failures support two modes:
  `delay` (countdown timer) and `condition` (FDM parameter monitoring with duration
  threshold). Condition params: airspeed_kt, altitude_ft_msl, altitude_ft_agl,
  vertical_speed_fpm, on_ground, fuel_total_kg. Operators: less_than, greater_than,
  equals, less_than_or_equal, greater_than_or_equal.
- REASON: Training scenarios require failures at specific flight conditions (e.g. engine
  failure at V1, fuel leak above FL100). Duration threshold prevents false triggers from
  transient parameter excursions.

- AFFECTS: sim_msgs (3 messages), src/systems/failures/ (full node), src/aircraft/c172/config/failures.yaml,
  src/aircraft/ec135/config/failures.yaml, launch/sim_full.launch.py (aircraft_id param).
  Does NOT modify: navigation_node, electrical_node, flight_model_adapter.

## 2026-03-12 — 15:30:00 - Claude Code

### Failure consumer wiring — adapter, electrical, navigation

- DECIDED: **IFlightModelAdapter::apply_failure()** — new virtual method on the adapter
  interface: `apply_failure(method, params_json, active)`. Default no-op. JSBSimAdapter
  overrides with JSBSim property tree mappings for 9 failure methods.
- REASON: Keeps failure injection decoupled from the adapter interface — adapters that
  don't support failures simply ignore the calls. params_json uses simple std::string
  find/substr parsing — no external JSON library, consistent with the rest of the codebase.

- DECIDED: **JSBSim property mappings** for failure methods:
  `set_engine_running` → `propulsion/engine[N]/set-running`
  `set_engine_thrust_scalar` → `propulsion/engine[N]/thrust-scalar`
  `set_engine_oil_pressure` → `propulsion/engine[N]/oil-pressure-psi`
  `set_engine_fire` → `propulsion/engine[N]/fire-now`
  `set_fuel_tank_drain` → `propulsion/tank[N]/contents-lbs` (active drain via map, applied each step)
  `set_pitot_failed` → `systems/pitot[N]/serviceable`
  `set_gear_unsafe_indication` → `gear/unit[N]/wow` (0=force unsafe, -1=release)
  `set_gear_unable_to_extend` → `gear/unit[N]/pos-norm` (0=stuck retracted, -1=release)
  `set_tail_rotor_failed` → `systems/tail-rotor/serviceable`
- REASON: Maps directly to JSBSim's property tree. Fuel drain uses a persistent
  `active_drains_` map<int,float> applied at 50Hz with 0.72 kg/L avgas density.
  Unknown methods log a warning and are silently ignored — no crash.

- DECIDED: **Electrical CB 3-state override model** — sim_failures sends
  `set_circuit_breaker_state` with state: `normal` / `popped` / `locked`.
  Stored in `cb_overrides_` map. `popped` = CB open but pilot can reset.
  `locked` = CB open, only IOS can release. `normal` = remove override.
  Component failures use solver's `injectFault(id, "fail")` / `clearFault(id)`.
- REASON: Training requires CB failures that the student can recover from (popped)
  and failures that persist until the instructor clears them (locked). Uses the
  existing ElectricalSolver fault injection API — no solver redesign needed.

- DECIDED: **Navigation receiver failure gating** — navigation_node subscribes to
  `/sim/failure_state` (FailureState). Before publishing NavigationState, each
  receiver is gated: if receiver ID is in `failed_nav_receivers`, all outputs
  for that receiver are zeroed and valid flag set to false. Gated receivers:
  NAV1, NAV2, ADF, GPS1, GPS2. DME is also invalidated if its paired NAV source
  has failed.
- REASON: Receiver failures are handled at the navigation_node output layer —
  the navaid_sim world signals remain valid (station is fine, receiver is broken).
  This is the correct separation: world environment vs aircraft equipment.

- AFFECTS: IFlightModelAdapter.hpp (new virtual), JSBSimAdapter.hpp/.cpp (implementation),
  flight_model_adapter_node.cpp (subscription), electrical_node.cpp (subscription + CB overrides),
  navigation_node.cpp (subscription + gating). Does NOT change: FailureState.msg,
  FailureCommand.msg, FailureInjection.msg, failures_node.cpp, failures.yaml files.

## 2026-03-13 — 14:30:00 - Claude Code

- DECIDED: navaid_sim subscribes to /sim/failure/navaid_commands (FailureInjection) to gate
  world navaid signals. Method set_navaid_station_failed: parses station_id from params_json,
  adds/removes from failed_stations_ set (uppercase). Output gating applied after fill_nav_output():
  if station ident matches failed set, signal_valid=false, signal_strength=0.0.
  NAV1, NAV2, ADF, and DME (co-located with NAV1 VOR) all gated independently.

- DECIDED: Added params_override_json field to FailureCommand.msg. When non-empty, failures_node
  uses it as params_json in the FailureInjection instead of catalog params. This enables
  world_navaid_station_failed where station_id is empty in YAML but filled at inject time by
  the instructor. Stored in active_params_override_ map so clear sends the same override.
  All failure routing still goes through failures_node — no bypass.

- REASON: FailureCommand previously had no way to carry runtime parameters. Direct injection
  from ios_backend would bypass failures_node, meaning navaid failures wouldn't appear in
  FailureState.active_failure_ids, couldn't be cleared via clear_all, and wouldn't record
  correctly in rosbag2. params_override_json keeps the single routing path intact.

- DECIDED: IOS backend adds GET /api/navaids/search?q=&types=&limit= endpoint. Parses
  earth_nav.dat (X-Plane format) at startup, caches in memory. Returns JSON array of
  {ident, name, type, freq_mhz, lat, lon, range_nm}. Search: ident prefix first, then name
  substring, sorted alphabetically within each group.

- DECIDED: IOS backend subscribes to /sim/failure_state, publishes FailureCommand to
  /ios/failure_command, loads failures.yaml catalog per aircraft. WebSocket handlers for
  failure_command (inject/clear/clear_all), failure_state forwarding, failures_config forwarding.

- DECIDED: FailuresPanel.jsx now renders actual failure catalog grouped by ATA chapter.
  Each failure has INJECT/CLR button. Failures with id starting "world_navaid" show a navaid
  search UI instead: debounced search input (300ms), type filter (ALL/VOR/ILS/NDB), results
  list with station selection, INJECT button that fills station_id into params_override_json.
  World failures (ata: null) shown under separate WORLD / ENVIRONMENT section.

- AFFECTS: FailureCommand.msg (added params_override_json), failures_node.cpp (inject_failure
  uses override, stores in active_params_override_, clear uses stored override),
  navaid_sim_node.cpp (failure subscription + station gating), ios_backend_node.py (failure
  state sub, failure cmd pub, failures config loader, navaid search endpoint),
  useSimStore.js (failure actions, failure_state/failures_config handlers),
  FailuresPanel.jsx (full rewrite with catalog display + navaid search UI).

---

## Co-simulation capability model: FDM_NATIVE / EXTERNAL_COUPLED / EXTERNAL_DECOUPLED

**Date:** 2026-03-13
**Status:** DESIGN DECISION — not yet implemented

### Problem

`FlightModelCapabilities` currently uses bool flags (e.g. `models_electrical = true/false`).
This is too coarse. There are three meaningfully different situations:

1. The FDM models the system well — we should defer entirely and read its output.
2. We model the system ourselves, and the FDM *needs our output* to stay physically coherent
   (e.g. bus voltage drives starter torque, fuel pump pressure drives fuel delivery).
3. We model the system ourselves, and the FDM doesn't use it internally at all.

Cases 2 and 3 are not the same. Case 2 requires writing our solver's output back into the
FDM as property overrides after each solve cycle, or the FDM's physics break — engine won't
start, fuel flow is wrong, etc. Case 3 requires no writeback.

### Decision

Replace bool flags in `FlightModelCapabilities` with a three-value enum per system:

```cpp
enum class CapabilityMode {
  FDM_NATIVE,           // FDM models it natively — our node defers, reads FDM output
  EXTERNAL_COUPLED,     // We model it, FDM needs our output — writeback required each cycle
  EXTERNAL_DECOUPLED    // We model it, FDM doesn't use it — no writeback needed
};

struct FlightModelCapabilities {
  CapabilityMode electrical  = CapabilityMode::FDM_NATIVE;
  CapabilityMode fuel        = CapabilityMode::FDM_NATIVE;
  CapabilityMode hydraulic   = CapabilityMode::FDM_NATIVE;
  CapabilityMode gear        = CapabilityMode::FDM_NATIVE;
  CapabilityMode pneumatic   = CapabilityMode::FDM_NATIVE;
  // ... extend as needed
};
```

Each system node reads the published capability and behaves accordingly:
- `FDM_NATIVE`          → node defers, does not run its own solver
- `EXTERNAL_COUPLED`    → node runs solver, then calls adapter write-back method each cycle
- `EXTERNAL_DECOUPLED`  → node runs solver, no write-back

### Write-back interface

Add per-system write-back virtual methods to `IFlightModelAdapter`:

```cpp
virtual void write_back_electrical(const sim_msgs::msg::ElectricalState & state) {}
virtual void write_back_fuel(const sim_msgs::msg::FuelState & state) {}
virtual void write_back_hydraulic(const sim_msgs::msg::HydraulicState & state) {}
```

Default implementation is a no-op — adapters only override what they need.
Each system node calls this after each solve cycle when mode is EXTERNAL_COUPLED.

### File structure for write-back implementations

Write-back property mappings are FDM-specific and must not live in a single monolithic
adapter file. Each FDM gets a subdirectory of helper translation units:

```
src/core/flight_model_adapter/
  include/flight_model_adapter/
    IFlightModelAdapter.hpp          ← interface + CapabilityMode enum
    JSBSimAdapter.hpp
    jsbsim/
      JSBSimElectricalWriteback.hpp
      JSBSimFuelWriteback.hpp
      JSBSimGearWriteback.hpp
    xplane/
      XPlaneElectricalWriteback.hpp
      XPlaneFuelWriteback.hpp
    matlab/
      MatlabElectricalWriteback.hpp  ← added when Matlab adapter arrives
  src/
    JSBSimAdapter.cpp                ← thin, delegates to writeback helpers
    jsbsim/
      JSBSimElectricalWriteback.cpp  ← all JSBSim property paths for electrical
      JSBSimFuelWriteback.cpp
      JSBSimGearWriteback.cpp
    xplane/
      XPlaneElectricalWriteback.cpp  ← all X-Plane datarefs for electrical
```

Write-back helpers are free functions (or static methods) taking the FDM executive
pointer and the state message. JSBSimAdapter owns the FDMExec* and passes it in.
No multiple inheritance — composition only.

### Capabilities as a published topic

Capabilities must be published as a latched ROS2 topic at startup:
  `/sim/flight_model/capabilities` (FlightModelCapabilitiesMsg, transient_local QoS)

Every system node subscribes and gates its behaviour on the received value.
This replaces the current pattern where capabilities are read once inside the adapter
node and never shared. Nodes that start before the capabilities topic is available
wait for it before activating.

### Current state (gap)

- `FlightModelCapabilities` has bool flags — not yet migrated to CapabilityMode enum
- `electrical_node`, `gear`, `hydraulic` do not gate on capabilities at all
- Only `fuel_node` correctly gates on `models_fuel_quantities` (bool, current impl)
- Capabilities are not published as a topic — only stored locally in adapter node
- `apply_failure()` writes to FDM properties regardless of capability mode

### Trigger

Implement before adding a second FDM adapter (X-Plane or Matlab).
The bool → enum migration can be done incrementally: convert one system at a time,
starting with electrical (highest coupling risk) then fuel, then gear.

### AFFECTS
IFlightModelAdapter.hpp, FlightModelCapabilitiesMsg, JSBSimAdapter,
all system nodes (electrical, fuel, gear, hydraulic), flight_model_adapter_node

---

## 2026-03-13 — 13:42:03 - Claude Code

- DECIDED: **Capability model IMPLEMENTED** — CapabilityMode tri-state enum (FDM_NATIVE / EXTERNAL_COUPLED / EXTERNAL_DECOUPLED) replaces bool flags in IFlightModelAdapter and FlightModelCapabilities.
- DECIDED: **FlightModelCapabilities.msg** created with uint8 constants (FDM_NATIVE=0, EXTERNAL_COUPLED=1, EXTERNAL_DECOUPLED=2) and 18 subsystem fields.
- DECIDED: **`/sim/flight_model/capabilities`** topic published by flight_model_adapter_node with transient_local QoS (latched, late-joining nodes receive it).
- DECIDED: **Writeback pattern** — system nodes publish to `/sim/writeback/<system>` (e.g. `/sim/writeback/electrical`, `/sim/writeback/fuel`). flight_model_adapter_node subscribes and routes to the concrete adapter's `write_back_*()` methods. Only active when capability mode == EXTERNAL_COUPLED.
- DECIDED: **JSBSim electrical writeback** writes `systems/electrical/bus-volts` property. **JSBSim fuel writeback** writes `propulsion/tank[N]/contents-lbs` per tank.
- DECIDED: JSBSimAdapter declares: electrical=EXTERNAL_COUPLED, fuel_quantities=EXTERNAL_COUPLED, fuel_pump_pressure=EXTERNAL_COUPLED. All others FDM_NATIVE.
- DECIDED: **electrical_node** gates solver on capabilities — skips update when FDM_NATIVE.
- DECIDED: **fuel_node** replaces legacy `cap_models_fuel_quantities` bool check with capabilities subscription. Publishes writeback when EXTERNAL_COUPLED.
- DECIDED: **navigation_node** subscribes to capabilities, stores latest_caps_ (no gating change yet — navigation is always external).
- REASON: Required for multi-FDM support. X-Plane models everything natively (all FDM_NATIVE), while JSBSim delegates electrical/fuel to our system nodes (EXTERNAL_COUPLED).
- AFFECTS: IFlightModelAdapter.hpp, FlightModelCapabilities.msg, JSBSimAdapter.hpp/cpp, JSBSimElectricalWriteback.cpp, JSBSimFuelWriteback.cpp, flight_model_adapter_node.cpp, electrical_node.cpp, fuel_node.cpp, navigation_node.cpp, sim_msgs/CMakeLists.txt
- DONE: ElectricalState bus_voltages[] was already present in the msg. JSBSimElectricalWriteback now iterates all buses, writing both indexed form ("systems/electrical/bus[N]/voltage") and named form ("systems/electrical/<bus_name>/voltage"), plus battery_soc_pct to "systems/electrical/battery/charge-pct". FuelState writeback functional. gear_node and hydraulic_node not yet wired to capabilities.

---

## 2026-03-13 — Claude Code

- DECIDED: **CIGI bridge implemented** — CIGI 3.3 over UDP, two components:
  1. `src/core/cigi_bridge/` → ROS2 lifecycle node (`sim_cigi_bridge` package, `cigi_bridge_node` executable). Raw CIGI 3.3 UDP encoding, NO CCL dependency. Builds clean on Linux.
  2. `~/x-plane_plugins/xplanecigi/` → X-Plane Windows plugin (`.xpl`), cross-compiled with mingw-w64 from WSL. Uses `CIGI_IG_Interface` (IG-side CCL wrapper) + `XPlaneData` adapters.
- DECIDED: **No CCL dependency in ROS2 package** — host-side encodes CIGI 3.3 packets with raw BE byte writes. CCL (CIGI Class Library) not installed; vendored CIGI_IG_Interface in `cigi_ig_interface/` subdir compiled only when CCL is found (cmake conditional, currently skipped).
- DECIDED: **Network stub** — Linux UDP implementation of `Network_NS::Network` / `InAddress` / `OutAddress` in `cigi_ig_interface/` satisfies CIGI_IG_Interface compile-time deps when CCL is available.
- DECIDED: **Interface adapters in XPluginMain.cpp** — `XPlaneEntityAdapter` bridges `IEntityCtrlProcessor` (uint16 entityType) → `XPlaneData::ChangeEntityType(string)`. `XPlaneHotAdapter` bridges `IHotHatProcessor` (lat/lon/alt args) → `XPlaneData::GetHat/GetHot(Position struct)`.
- DECIDED: **HatHotResponse.msg** — new message in sim_msgs, published on `/sim/cigi/hat_responses` (circular buffer of 16 in-flight requests via `HatRequestTracker`).
- DECIDED: **Sign convention** — host sends `pitch_rad`, `roll_rad`, `true_heading_rad` (all in radians, from FlightModelState). cigi_bridge_node converts rad→deg. X-Plane plugin negates pitch and bank in `SetPositionAndOrientation()` (as per existing EntityCtrl.cpp pattern).
- DECIDED: **Packet format** — one UDP datagram per send cycle: IG Control (24 bytes) + Entity Control (48 bytes), big-endian as per CIGI 3.3 ICD.
- REASON: CIGI 3.3 is the established IG protocol; decoupled X-Plane from the sim so the IG can be swapped without changes to ROS2 nodes.
- AFFECTS: sim_msgs/CMakeLists.txt (HatHotResponse.msg), src/core/cigi_bridge/ (all new), ~/x-plane_plugins/xplanecigi/ (XPluginMain.cpp + stubs + CMakeLists.txt)
- TODO (blockers): X-Plane SDK needed for plugin build (download from developer.x-plane.com). CCL needed to enable vendored CIGI_IG_Interface compilation. Plugin NOT yet tested end-to-end.

---

## 2026-03-13 — 18:30 - Claude Code

- DECIDED: **xplanecigi.xpl rewritten as raw CIGI 3.3 IG plugin** — no CCL, no CIGI_IG_Interface, no XPlaneData/XPlaneConfig/adapter headers. Single source file (XPluginMain.cpp). Parses host→IG datagrams directly with big-endian byte reads; sends 32-byte SOF reply after each datagram.
- DECIDED: **Packets handled**: 0x01 IG Control (extract host frame counter from bytes [8-11]); 0x03 Entity Control (roll/pitch/yaw float32 BE at [12/16/20] degrees, lat/lon/alt float64 BE at [24/32/40] degrees/metres). X-Plane sign convention: phi=−roll, theta=−pitch, psi=yaw.
- DECIDED: **Network config via xplanecigi.ini** — file read from plugin directory at startup using XPLMGetPluginInfo(XPLMGetMyID()). Keys: host_ip, ig_port, host_port. Fallback defaults: 127.0.0.1 / 8002 / 8001. Logs config path loaded or "not found — using defaults".
- DECIDED: **libwinpthread statically linked** — `-Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic` added to CMakeLists.txt. Remaining DLL deps: XPLM_64.dll, KERNEL32.dll, msvcrt.dll, WS2_32.dll — all provided by Windows/X-Plane, no mingw runtime required on target machine.
- REASON: CCL (CIGI Class Library) not available; raw parser is simpler, dependency-free, and sufficient for the ownship entity control + SOF use case. libwinpthread was causing Error Code 126 (DLL not found) on X-Plane load.
- STATUS: Plugin loads in X-Plane. End-to-end CIGI link not yet tested.
- AFFECTS: ~/x-plane_plugins/xplanecigi/XPluginMain.cpp, CMakeLists.txt (stripped to XPluginMain.cpp + SDK + ws2_32)

## 2026-03-15 — 15:00:00 - Claude Code

- DECIDED: **Multi-point HOT terrain system** — full pipeline from gear contact points through CIGI to JSBSim terrain elevation
- DECIDED: `gear_points` section added to aircraft config.yaml (body-frame x/y/z offsets for nose, left_main, right_main)
- DECIDED: New messages: `TerrainSource.msg` (SOURCE_CIGI/SRTM/MSL/UNKNOWN), `HotRequest.msg`, `GetTerrainElevation.srv`
- DECIDED: `HatHotResponse.msg` extended with `string point_name` for multi-point correlation
- DECIDED: `navaid_sim` exposes `/navaid_sim/get_terrain_elevation` ROS2 service using existing SRTM data
- DECIDED: `cigi_bridge` sends CIGI HOT Request packets (0x18, 32 bytes) for each gear point, rate-gated by AGL: 50Hz <10m, 10Hz 10-100m, off >100m
- DECIDED: `hat_request_tracker` capacity increased to 32, now stores point_name per entry
- DECIDED: `flight_model_adapter` is terrain consumer: subscribes to `/sim/cigi/hat_responses`, SRTM fallback via service client, publishes `TerrainSource` on `/sim/terrain/source` at 1Hz
- DECIDED: CIGI HOT timeout = 2s before falling back to SRTM, then to 0.0 MSL
- DECIDED: IC altitude override: queries terrain at IC position, uses terrain+0.5m for on-ground starts
- DECIDED: Near-ground terrain writeback: sets JSBSim `position/terrain-elevation-asl-ft` when AGL<2m and HOT data available
- DECIDED: IOS StatusStrip terrain indicator: green dot "CIGI HOT", yellow dot "SRTM", red dot "MSL", grey dot "NO TERR"
- DECIDED: X-Plane CIGI plugin reference implementation (XPluginMain.cpp) — handles 0x18 HOT requests via XPLMProbeTerrainXYZ, sends 0x02 48-byte responses
- REASON: Terrain elevation needed for correct on-ground behavior (gear contact, takeoff roll, GPWS) and visual/physics synchronization
- AFFECTS: sim_msgs, navaid_sim, sim_cigi_bridge, flight_model_adapter, ios_backend, ios/frontend, aircraft config

## 2026-03-15 — 16:30:00 - Claude Code

- DECIDED: **IC always applied immediately** — raw IC goes to JSBSim right away, SRTM async response re-applies with adjusted altitude when it arrives. Previous approach left a gap (no IC applied until async SRTM response) which caused JSBSim to run from default airborne state and fall.
- DECIDED: **Terrain elevation written every frame** (when HOT data exists), not gated by AGL < 2m. The AGL < 2m guard created a chicken-and-egg problem: JSBSim terrain stuck at 0ft → AGL = MSL → AGL always > 2m → terrain never updated. CIGI rate gating (off above 100m) already limits data rate.
- DECIDED: **Guard retained**: terrain is never set above aircraft altitude + 0.5m (prevents negative AGL → JSBSim assertion crash in FGTable).
- DECIDED: **`get_property()` added to IFlightModelAdapter interface** and JSBSimAdapter — wraps `exec_->GetPropertyValue()`. Used to verify terrain writes.
- DECIDED: **`position/terrain-elevation-asl-ft` IS writable** in JSBSim v1.2.1 — the property is tied with both getter and setter (FGPropagate → FGInertial → FGDefaultGroundCallback::mTerrainElevation). Confirmed via read-back debug logging.
- DECIDED: **Actual xplanecigi plugin updated** (`~/x-plane_plugins/xplanecigi/XPluginMain.cpp`) — added 0x18 HOT Request handler with XPLMProbeTerrainXYZ terrain probe, sends 48-byte 0x02 HOT Response. The reference copy in `cigi_ig_interface/` is for non-Windows/standalone builds only.
- DECIDED: **input_arbitrator brake fix** — `to_flight()` was missing `brake_left` and `brake_right` copy from RawFlightControls → FlightControls. Brakes were silently zeroed. Fixed.
- REASON: JSBSim FGTable assertion crash (`Factor >= 0.0 && Factor <= 1.0`) caused by aircraft falling from 100m MSL with 0 airspeed (no IC applied, terrain at 0ft). Multiple fixes needed to close all failure paths.
- AFFECTS: flight_model_adapter_node.cpp, IFlightModelAdapter.hpp, JSBSimAdapter.hpp/.cpp, input_arbitrator_node.cpp, ~/x-plane_plugins/xplanecigi/XPluginMain.cpp

## 2026-03-15 — 19:00:00 - Claude Code

- DECIDED: **Airport/runway database in navaid_sim** — new `AirportDatabase` class with A424 (euramec.pc PA/PG records) and XP12 (apt.dat row codes 1/1302/100) parsers. Loaded at startup, queryable via ROS2 services.
- DECIDED: **New messages**: `Airport.msg`, `Runway.msg`. **New services**: `SearchAirports.srv`, `GetRunways.srv` in sim_msgs.
- DECIDED: **Airport DB uses apt.dat by default** (not euramec.pc) — A424 runway coordinates differ from X-Plane visual scenery by 40-130m due to different survey sources. apt.dat coordinates match the IG exactly. Launch param: `airport_db_format: xp12`, `airport_db_path: apt.dat`.
- DECIDED: **A424 runway heading field was at wrong column offset** — parser read displaced threshold (pos 51) instead of magnetic bearing (pos 27). Fixed: heading now parsed from cols 28-31 (×10 degrees).
- DECIDED: **JSBSim `SetLatitudeDegIC()` sets GEOCENTRIC latitude** — was causing ~15km northward offset at European latitudes. Fixed: use `SetGeodLatitudeDegIC()` for geodetic latitude. Both the default IC and `apply_initial_conditions()` corrected.
- DECIDED: **IOS POS page redesigned** — live airport search (debounced, min 2 chars, via SearchAirports service), runway end selection buttons, position icons (RWY/2NM/4NM/DWN). BASE and XWIND removed. Position computed from threshold coordinates + runway heading + offset math.
- DECIDED: **IC published directly from IOS** — `ios_backend` publishes `InitialConditions` on `/sim/initial_conditions` when position icon clicked. No CMD_RESET needed — FDM applies IC immediately, CIGI bridge updates IG.
- DECIDED: **IC configuration auto-selects** — `airborne_clean` when airspeed > 1 m/s, `ready_for_takeoff` when on ground (0 airspeed). Prevents `ready_for_takeoff` from forcing 0kt and `force-on-ground` on airborne positions.
- DECIDED: **`get_property()` added to IFlightModelAdapter** — wraps `exec_->GetPropertyValue()`, used for terrain elevation read-back verification.
- DECIDED: **Backend service calls use async polling** (not `spin_until_future_complete`) — avoids "node already in executor" crash. Background `rclpy.spin` thread resolves futures; WS handlers poll with `await asyncio.sleep(0.05)`.
- REASON: IOS needs to position aircraft at specific runway thresholds for training scenarios. Coordinate precision requires matching IG data source (apt.dat) and correct geodetic/geocentric handling in JSBSim.
- AFFECTS: sim_msgs, navaid_sim (AirportDatabase + services), ios_backend (IC publisher + WS handlers), flight_model_adapter (JSBSimAdapter geodetic fix), ios/frontend (PositionPanel), launch file

## 2026-03-18 — 22:45:00 - Claude Chat (Architecture Session)

### DECIDED: Engines Node Design — IEnginesModel Plugin Interface

**Context:** Designed engines node after analyzing three FDM ICDs: JSBSim (C172 piston), Helisim (EC135 twin turboshaft), J2/EURAMEC (C208 turboprop). All three FDMs handle engine physics internally. The engines node is a state machine and orchestrator, not a physics model.

**Architecture:**
- Follows existing pattern: standalone C++ library (solver/state machine) + thin ROS2 node wrapper
- Aircraft-specific behavior via IEnginesModel pluginlib plugin
- Plugin has FULL authority over what gets published — can passthrough FDM outputs directly, override them, or add custom failure effects
- Config (engines.yaml) declares parameters; C++ plugin implements behavior (not data-driven for procedural logic)

**IEnginesModel interface:**
- initialize(config_path, fdm_adapter) — load YAML params, store FDM reference
- update(dt, inputs, active_failures) -> EngineStateData — called every tick
- set_running(engine_index, running) — autostart/instant position
- get_engine_count() -> uint8

**EngineInputs struct (fixed superset, not generic map):**
- Power management (float32, normalized 0-1): throttle[4], power_lever[4], prop_lever[4], condition_lever[4], mixture[4], emergency_power[4]
- Discrete switches: starter[4], ignition[4], fuel_cutoff[4]
- Systems coupling (filled by framework): bus_voltage, fuel_available[4]
- REASON for fixed struct over generic map: input arbitrator needs to validate/route, IOS needs known field names for widgets

**EngineStateData struct (float32, zeros for unused fields):**
- Per-engine [4]: state enum (OFF/CRANKING/STARTING/RUNNING/SHUTDOWN/FAILED), n1_pct, n2_pct, engine_rpm, itt_degc, egt_degc, tot_degc, oil_press_kpa, oil_temp_degc, fuel_flow_kgph, torque_nm, torque_pct, shp_kw, manifold_press_inhg, starter_engaged, generator_online
- Drivetrain (scalar, not per-engine): prop_rpm, main_rotor_rpm, tail_rotor_rpm
- REASON for float32: instrument-grade precision sufficient, saves bandwidth at 60Hz
- REASON for separate itt/egt/tot: technically different measurements at different engine locations
- REASON for drivetrain as scalar: rotor RPM driven by ALL engines through combining gearbox

**Failure handling — dual mode:**
- Passthrough: plugin translates framework failure IDs to FDM flags (e.g., J2 boolean switches)
- Plugin-handled: plugin implements failure effects when FDM lacks support
- engines.yaml declares which failures are passthrough vs plugin-handled
- Failure IDs always come through failures_node pipeline (non-negotiable)

**Three initial plugin targets:**
1. C172/JSBSim — piston single. throttle+mixture. Most failures plugin-handled.
2. EC135/Helisim — twin turboshaft. FADEC in plugin, N1/N2, tot_degc, main_rotor_rpm + tail_rotor_rpm.
3. C208/J2 — turboprop single. power_lever/prop_lever/condition_lever. Most failures passthrough to J2.

**Key insight from C208 ICD:** J2 FDM takes Voltage and Fuel Fraction as inputs from electrical/fuel nodes via FDM adapter, NOT through engines node. EngineInputs provides bus_voltage and fuel_available so plugin state machine knows if starting is possible.

- AFFECTS: sim_msgs, src/systems/engines/, aircraft plugins, engines.yaml, IOS engine page, input_arbitrator

## 2026-03-21 — 00:00:00 - Claude Code
- DECIDED: Expanded IGearModel interface to include GearSnapshot/GearLegState data structures and full update() signature (gear_on_ground, gear_position_pct, gear_steering_deg, gear_handle_down, on_ground vectors)
- REASON: Stub interface only had configure/update(double)/apply_failure — no way to pass FDM gear data in or read state out. New interface matches the IElectricalModel pattern with a snapshot struct.
- AFFECTS: src/core/sim_interfaces/include/sim_interfaces/i_gear_model.hpp, all IGearModel implementors

- DECIDED: gear_node subscribes to /sim/controls/flight for gear_handle_down, brake_left/right, parking_brake — these are echoed into GearState for single-source display
- REASON: GearState.msg includes brake fields; gear_node is the natural place to aggregate them since it already owns WoW data from FlightModelState
- AFFECTS: src/systems/gear/src/gear_node.cpp

- DECIDED: C172 GearModel reads gear_type and retractable from gear.yaml; uses wheel_angle_deg[0] from FlightModelState for nosewheel angle; supports "gear_unsafe_indication" failure ID for warning light test
- REASON: Fixed gear has no retraction logic — all complexity is in correctly reflecting FDM ground contact and steering. Failure injection for unsafe indication needed for IOS failure panel
- AFFECTS: src/aircraft/c172/src/gear_model.cpp, src/aircraft/c172/config/gear.yaml

- DECIDED: gear_node capability gate uses FlightModelCapabilities::gear_retract field (not a new field)
- REASON: gear_retract already exists in FlightModelCapabilities.msg — no message changes needed
- AFFECTS: src/systems/gear/src/gear_node.cpp

## 2026-03-21 — 09:00:00 - Claude Code
- DECIDED: Added AirDataState.msg to sim_msgs — published by sim_air_data on /sim/air_data/state at 50 Hz
- REASON: Cockpit displays and IOS must read instrument values (IAS, altitude, VSI) from the pitot-static model rather than directly from FlightModelState. Separating instrument outputs from truth state is required so that pitot/static blockage, alternate static, QNH correction, and position-error correction all affect displayed values without touching FlightModelState.
- AFFECTS: src/sim_msgs/msg/AirDataState.msg (new), src/sim_msgs/CMakeLists.txt (registered under rosidl_generate_interfaces)

## 2026-03-21 — 00:00:00 - Claude Code
- DECIDED: sim_air_data implemented as new core system node with pluginlib pattern matching sim_electrical/sim_gear
- DECIDED: IAirDataModel interface with AirDataInputs/AirDataSnapshot/AirDataSystemState structs; two additional methods get_heat_load_names() and get_alternate_static_switch_ids() called after configure() so the node knows which ElectricalState load names and PanelControls switch IDs to search for at runtime
- DECIDED: Pitot-static physics: blocked pitot (drain clear/drain blocked), blocked static port, alternate static with configurable pressure offset
- DECIDED: Icing model: binary accumulation (visible moisture + OAT < 5°C + no pitot heat → accumulates over configurable delay, clears at 2x rate with heat on)
- DECIDED: Turbulence on pitot: band-limited noise (0.5s first-order IIR low-pass) scaled by turbulence_intensity × TAS × configurable gain added to pitot pressure
- DECIDED: Pitot heat state derived from ElectricalState.load_powered (not switch position) — CB popped means no heat even if switch is on
- DECIDED: AirDataState.msg supports up to 3 pitot-static systems (captain/FO/standby). C172 has 1 system.
- DECIDED: air_data is always EXTERNAL_DECOUPLED — we always compute instrument values, no FDM writeback needed. No FlightModelCapabilities gating applied.
- DECIDED: VSI uses first-order lag filter (1.5s time constant) matching real instrument response; blocked static with normal port selected forces VSI to zero
- DECIDED: Visible moisture proxy: cloud_coverage >= BKN(3) from WeatherState AND aircraft altitude > 50% of cloud base in metres (rough in-cloud detection); turbulence_intensity read directly from WeatherState.turbulence_intensity field
- DECIDED: In-flight static pressure falls back to FlightModelState.static_pressure_pa when atmosphere_node has not yet published; same for temperature/density
- REASON: All three FDMs (JSBSim, J2, Helisim) output truth air data with no meaningful pitot-static failure model. Pitot icing recognition is a required FNPT II training scenario. Instrument air data must be separate from truth for correct failure training.
- AFFECTS: src/core/sim_interfaces/include/sim_interfaces/i_air_data_model.hpp (new), src/systems/air_data/ (new package: CMakeLists.txt, package.xml, src/air_data_node.cpp), src/aircraft/c172/src/air_data_model.cpp (new plugin), src/aircraft/c172/config/air_data.yaml (new), src/aircraft/c172/plugins.xml (AirDataModel entry added), src/aircraft/c172/CMakeLists.txt (air_data_model.cpp added to library sources)

## 2026-03-21 — 10:00:00 - Claude Code
- DECIDED: ios_backend subscribes to /sim/air_data/state (AirDataState) and forwards system index 0 as a `air_data_state` WS message with converted fields: ias_kt, cas_kt, mach, alt_indicated_ft, alt_pressure_ft, vs_fpm, sat_c, tat_c, pitot_healthy, static_healthy, pitot_heat_on, pitot_ice_pct.
- DECIDED: controls.html six-pack split by instrument type — ASI/ALT/VSI read from airData state object (pitot-static, index 0 from air_data_state); AI/HI/TC stay on fms (flight_model_state truth). Position bar continues to read fms truth.
- DECIDED: pitot-static health indicator added below the six-pack in controls.html. Implemented via DOM manipulation (createElement/textContent/appendChild) instead of innerHTML to avoid XSS risk. Shows "PITOT FAIL" (red) + "STATIC FAIL" (red) + "ICE XX%" (amber) only when relevant; empty when all healthy.
- REASON: controls.html is the keyboard controls debug page used during development. Having it show instrument values (affected by failures/icing) rather than truth values makes it useful for testing the sim_air_data node and pitot failure scenarios. The IOS instructor position bar still shows truth alt/hdg for situational awareness.
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py (AirDataState import, subscription, _on_air_data_state callback), ios/frontend/controls.html (airData object, WS handler, gauge calls, pitot-static-status div, updatePitotStaticStatus function)

## 2026-03-21 — 12:00:00 - Claude Code

- DECIDED: Failure injection routed to sim_air_data via /sim/failure/air_data_commands. failures_node gains "air_data" handler routing. C172 failures.yaml updated: old ata34_pitot_failed (flight_model handler) and ata34_altimeter_failed (sim_failures handler) replaced with three air_data-routed failures: pitot_blocked_drain_clear, pitot_blocked_drain_blocked, static_port_blocked.
- REASON: Pitot/static failures are now modeled in sim_air_data, not JSBSim. Routing through failures_node ensures failures appear in FailureState, can be armed/cleared via IOS, and record in rosbag2.
- AFFECTS: failures_node.cpp (air_data_cmd_pub_), src/aircraft/c172/config/failures.yaml

- DECIDED: Added bool visible_moisture to WeatherState.msg — instructor-set flag for icing conditions. sim_air_data reads this directly instead of deriving from cloud coverage + altitude heuristic.
- REASON: Instructor should explicitly control icing conditions for training scenarios. Derivation from cloud base/coverage was unreliable and added unnecessary complexity.
- AFFECTS: sim_msgs/msg/WeatherState.msg, src/systems/air_data/src/air_data_node.cpp

- DECIDED: Removed sim_hydraulic, sim_ice_protection, sim_pressurization from C172 launch. C172 has no hydraulic system, no pressurization, and pitot heat is handled via electrical load + sim_air_data. Stub nodes remain in src/systems/ for future aircraft.
- REASON: Launching nodes that do nothing wastes resources and clutters the node list. Aircraft config required_nodes drives what gets launched.
- AFFECTS: launch/sim_full.launch.py, src/aircraft/c172/config/config.yaml

## 2026-03-21 — 15:00:00 - Claude Code

- DECIDED: ios_backend rclpy.spin() replaced with spin_once(timeout_sec=0.05) in a dedicated thread with 3s initial delay. rclpy.spin() in a daemon thread silently fails under uvicorn — callbacks never fire.
- REASON: Root cause of all IOS data flow issues. The spin thread appeared alive but processed no subscription callbacks. DDS discovery also needs ~2-3s before messages arrive.
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py (startup_event)

- DECIDED: Custom JSON encoder (_RosEncoder) for ios_backend. ROS2 message arrays return numpy.bool_/int_/float_ types which crash json.dumps. Encoder converts numpy types to native Python. Applied to broadcast and per-client WS sender.
- REASON: json.dumps('Object of type bool_ is not JSON serializable') silently killed the WS sender task. Commands still worked (different code path) but no data flowed to clients.
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py

- DECIDED: WS sender uses JSON string comparison instead of object identity (is not) for change detection. Breaks cleanly on exception instead of silently dying.
- REASON: Identity comparison across threads was unreliable. Unhandled exceptions in sender killed data flow while connection stayed open.
- AFFECTS: src/ios_backend/ios_backend/ios_backend_node.py

- DECIDED: sim_manager publishes SimState at 10Hz unconditionally (no dedup). Late-joining nodes receive current state immediately.
- REASON: Previous dedup meant ios_backend starting after the sim never received the current state, falling back to stub READY.
- AFFECTS: src/core/sim_manager/src/sim_manager_node.cpp

## 2026-03-21 — 16:00:00 - Claude Code

- DECIDED: Two-step IC terrain — apply with SRTM first, then refine with CIGI HOT within 2s. Stale CIGI HOT data cleared on new IC. JSBSim terrain-elevation-asl-ft set BEFORE apply_initial_conditions so force-on-ground has correct ground plane.
- REASON: Aircraft was ending up underground because JSBSim's internal terrain was stale. Setting terrain first, then IC, lets force-on-ground place gear correctly.
- AFFECTS: src/core/flight_model_adapter/src/flight_model_adapter_node.cpp

- DECIDED: IOS position icons (RWY, 2NM, 4NM, DWN) now preview only — require SET POSITION button with double-tap confirm before applying IC.
- REASON: Clicking a position icon immediately repositioned the aircraft with no confirmation, which was unexpected and disorienting.
- AFFECTS: ios/frontend/src/components/panels/PositionPanel.jsx

- DECIDED (PLANNED): IC terrain will be simplified to: position at 0 MSL → wait for CIGI HOT → reposition to correct height. Eliminates SRTM dependency for on-ground starts when IG connected. SRTM remains fallback when no IG.
- REASON: Current 3-stage IC (raw → SRTM → CIGI refine) is complex. Positioning at 0 MSL first guarantees IG pages terrain at target position before probing.
- AFFECTS: flight_model_adapter_node.cpp (future change)

## 2026-03-22 — 10:00:00 - Claude Code

- DECIDED: Add REPOSITIONING state (value 6) to sim_manager state machine. Entered on CMD_SET_IC from any non-INIT/non-SHUTDOWN state. Exits to READY on /sim/terrain/ready (Bool=true) or after 5s timeout.
- REASON: Aircraft repositioning needs to pause the sim clock while waiting for terrain (CIGI HOT or SRTM) to resolve at the new position, preventing the aircraft from falling through unloaded terrain.
- AFFECTS: src/core/sim_manager/src/sim_manager_node.cpp, src/sim_msgs/msg/SimState.msg (already had STATE_REPOSITIONING=6)

## 2026-03-22 — 10:30:00 - Claude Code

- DECIDED: flight_model_adapter publishes /sim/terrain/ready (std_msgs/Bool) after IC terrain is resolved. Published false on IC receipt, true when CIGI HOT or SRTM terrain applied, or on 5s hard timeout.
- REASON: sim_manager REPOSITIONING state subscribes to this topic to know when terrain is resolved and it can transition to READY.
- AFFECTS: src/core/flight_model_adapter/src/flight_model_adapter_node.cpp

- DECIDED: gear_cg_height_m loaded from aircraft config.yaml gear_points (abs value of max z_m) instead of ROS2 parameter. Default 0.5m if config unavailable.
- REASON: Gear height is aircraft-specific data that belongs in the aircraft config, not a ROS2 parameter. Follows the pattern of aircraft config driving everything.
- AFFECTS: src/core/flight_model_adapter/src/flight_model_adapter_node.cpp, src/core/flight_model_adapter/CMakeLists.txt (added yaml-cpp, ament_index_cpp deps)

- DECIDED: Simplified IC terrain pipeline — CIGI HOT is the primary source (applied after 200ms), SRTM is fallback only after 2s timeout with no CIGI. Removed the old "apply SRTM immediately, then refine with CIGI" two-stage approach.
- REASON: With the REPOSITIONING state holding the clock, there is no urgency to apply SRTM immediately. Waiting for the best source (CIGI) avoids unnecessary double-repositioning.
- AFFECTS: src/core/flight_model_adapter/src/flight_model_adapter_node.cpp (IC callback + update loop terrain refinement block)

## 2026-03-22 — 14:30:00 - Claude Code
- DECIDED: cigi_bridge now parses SOF IG Status (byte 4 bits[1:0]) from IG response packets. ig_status_ member tracks IG mode (0=Standby, 1=Reset, 2=Operate). Logs transitions.
- REASON: Needed to know when IG has terrain loaded (Operate mode) so HOT requests can be sent during REPOSITIONING even when AGL is unknown/stale.
- AFFECTS: src/core/cigi_bridge/src/cigi_host_node.cpp, src/core/cigi_bridge/include/cigi_bridge/cigi_host_node.hpp

- DECIDED: HOT request rate gating bypassed during REPOSITIONING when ig_status_ == 2 (Operate). All gear point HOT requests sent every frame regardless of AGL altitude.
- REASON: During REPOSITIONING the sim needs ground elevation at the new position before transitioning to READY. Normal AGL gating (off above 100m) would prevent HOT requests when aircraft is repositioned to a new location with unknown terrain.
- AFFECTS: src/core/cigi_bridge/src/cigi_host_node.cpp (send_hot_requests)

- DECIDED: cigi_bridge subscribes to /sim/state to track sim_state_. Subscription created in on_activate, cleaned up in on_deactivate.
- REASON: Required to detect REPOSITIONING state for the HOT rate gating bypass.
- AFFECTS: src/core/cigi_bridge/src/cigi_host_node.cpp, src/core/cigi_bridge/include/cigi_bridge/cigi_host_node.hpp

## 2026-03-22 — 18:00:00 - Claude Code

- DECIDED: Repositioning pipeline implemented end-to-end. STATE_REPOSITIONING = 6 added to SimState.msg. sim_manager transitions to REPOSITIONING on IC, subscribes to /sim/terrain/ready, timeout at 35s. flight_model_adapter publishes terrain_ready, loads gear height from config.yaml gear_points. X-Plane plugin monitors async_scenery_load_in_progress, sends SOF IG Status (Standby/Operate), shows "REPOSITIONING..." overlay. cigi_bridge parses SOF IG Status, bypasses HOT rate gating during REPOSITIONING. IOS shows purple REPOSITIONING badge.
- DECIDED: Repositioning timeouts — CIGI HOT preferred (no limit), SRTM fallback at 30s, sim_manager exits REPOSITIONING at 35s, hard cleanup at 60s.
- DECIDED: sim_manager subscribes to /sim/initial_conditions directly — IOS publishes ICs bypassing command handler, sim_manager needs to detect these for REPOSITIONING transition.
- KNOWN ISSUE: X-Plane async_scenery_load_in_progress reports "loaded" before high-resolution terrain tiles are fully paged. Rapid repositions to distant airports may get stale/low-LOD HOT data at 200ms. First reposition works correctly (17s wait), subsequent nearby repositions are fast. Needs investigation — may need probe stability check in addition to dataref.
- AFFECTS: SimState.msg, sim_manager, flight_model_adapter, cigi_bridge, X-Plane plugin, ios_backend, IOS frontend

## 2026-03-23 — Claude Code

### Repositioning pipeline refactored — CIGI 3.3 compliant handshake

- DECIDED: **STATE_REPOSITIONING removed** from SimState.msg. Repositioning uses FROZEN state + `reposition_active` bool flag in SimState. Simpler state machine, no new state transitions needed.
- DECIDED: **CMD_REPOSITION = 11** added to SimCommand.msg. sim_manager owns the full workflow: save pre-reposition state → FROZEN → broadcast IC → wait for terrain_ready or 15s timeout → return to saved state. Rejects INIT, SHUTDOWN, RESETTING states.
- DECIDED: **IOS sends CMD_REPOSITION** (not direct IC publish). ios_backend set_departure handler creates SimCommand with CMD_REPOSITION, sim_manager controls the workflow.
- DECIDED: **CIGI IG Mode handshake** — cigi_bridge reads `reposition_active` from SimState. On rising edge: sends IG Mode = Reset (0x05) for one frame, clears HAT tracker (prevents stale HOT from old position). Subsequent frames: IG Mode = Operate (0x06). HOT requests at 10 Hz during reposition (not 60 Hz on every freeze). HOT responses only accepted when SOF IG Status = Operate.
- DECIDED: **X-Plane plugin driven by IG Mode** (not position-delta detection). Removed: g_repositioning flag, position threshold heuristic, settle timer, REPOSITIONING overlay. Added: parse IG Mode from IG Control byte 3 bits[1:0]. On Reset→Operate transition: begin terrain probe stability (4×0.5s probes within 1m). SOF: Standby during probing, Normal when stable.
- DECIDED: **pending_ic_ timeout = 30s** in flight_model_adapter. Covers X-Plane tile loading (observed 10-20s for cross-continent repositions). Sim stays FROZEN with REPOSITIONING badge during wait. No-CIGI fallback: waits 30s then accepts runway DB altitude.
- DECIDED: **FMA stepping gated by pending_ic_** — JSBSim does not step() while waiting for terrain HOT. Prevents DDS race where IC arrives before FROZEN state.
- DECIDED: **refine_terrain_altitude uses RunIC** with cockpit state save/restore. Avoids SetPropertyValue("position/h-sl-ft") which can corrupt FGLocation geodetic cache.
- DECIDED: **IOS REPOSITIONING badge** — ios_backend forwards reposition_active to frontend via WS. Frontend store maps to simState='REPOSITIONING' which shows purple badge and locks RUN/FREEZE buttons.
- DECIDED: **Bug #0 root cause was zombie process** — stale cigi_bridge from previous session held port 8001, sent Entity Control with old position at 60 Hz alongside new process. Fix: kill stale processes before start (`fuser -k 8001/udp 8002/udp`). Defensive code (position cache, immediate publish) was removed as unnecessary.
- REASON: Previous architecture used position-delta heuristics in X-Plane plugin and STATE_REPOSITIONING in the state machine. Fragile: plugin guessed when reposition happened, settle timers were arbitrary, stale HOT responses accepted. New architecture: host explicitly controls the handshake via CIGI protocol, plugin reports terrain readiness, HAT tracker invalidation prevents stale HOT.
- AFFECTS: SimState.msg, SimCommand.msg, sim_manager, flight_model_adapter, JSBSimAdapter, cigi_bridge (host + header + tracker), X-Plane plugin (XPluginMain.cpp), ios_backend, useSimStore.js, StatusStrip.jsx, ActionBar.jsx

### Stale HOT fix + terrain loading timeout + runway placement

- DECIDED: **hat_tracker_.clear() on reposition start** — discards in-flight HOT responses from the old position. Without this, stale HOT from the previous airport slips through the ig_status gate (old SOF Operate arrives in the same recv cycle as the stale HOT). Only HOT responses for requests sent AFTER reposition starts are accepted.
- DECIDED: **pending_ic_ timeout increased to 30s** (was 5s). X-Plane needs 10-20s to load DSF terrain tiles for cross-continent repositions. The 5s timeout was firing before IG reported terrain ready, leaving the aircraft at the runway DB altitude. Nearby airports (tiles cached) complete in ~2s. Observed: Las Vegas 11s, LAX 12s, Antwerp 20s (first visit).
- DECIDED: **apt.dat displaced threshold is in metres** — AirportDatabase.cpp was multiplying by FT2M (treating metres as feet), shrinking the value to 30%. Fixed: no conversion for apt.dat (already metres). ARINC-424 loader correctly uses FT2M (stores feet).
- DECIDED: **Ground placement: displaced_threshold + 30m offset** along runway heading. The 30m clears the piano bar markings — apt.dat threshold coordinates are at the physical pavement end, not at the markings. For runways with displaced thresholds, the offset also advances past the displaced area.
- AFFECTS: cigi_bridge (hat_request_tracker), flight_model_adapter, AirportDatabase.cpp, PositionPanel.jsx

## 2026-03-24 — Claude Code

- FIXED: Bug #7 — finish_reposition() now checks required node health before resuming RUNNING. Stays FROZEN + SimAlert if any node is LOST/OFFLINE.
- FIXED: Bug #12 — cigi_bridge uses aircraft_id parameter instead of hardcoded c172 config path.
- FIXED: Bug #13 — reload_node() checks each lifecycle transition return code, stops chain and logs error on failure.
- AFFECTS: sim_manager_node.cpp, cigi_host_node.cpp, BUGS.md

## 2026-03-24 — Design Intent (Chat) + Claude Code

- DECIDED: All ROS2 messages, services, and config files carry lat/lon in degrees, not radians. Internal solver code (navaid_sim, AirportDatabase) may use radians for trig — conversion happens at the ROS2 boundary only. JSBSimAdapter is the single rad→deg conversion point for FlightModelState.
- REASON: Helisim outputs degrees, CIGI needs degrees, IOS displays degrees, pilots read degrees. Only JSBSim uses radians natively. Unit audit showed every consumer immediately converting rad→deg on receipt — N redundant conversions eliminated.
- AFFECTED MESSAGES: FlightModelState.msg (latitude_deg/longitude_deg), InitialConditions.msg, HatHotResponse.msg (lat_deg/lon_deg/hat_m/hot_m), HotRequest.msg, Airport.msg (arp_lat_deg/arp_lon_deg), Runway.msg (threshold_lat_deg_end1/end2, threshold_lon_deg_end1/end2), GetTerrainElevation.srv.
- AFFECTED CODE: JSBSimAdapter (add RAD_TO_DEG), cigi_bridge (remove RAD_TO_DEG), ios_backend (remove 180/pi), navigation_node (passthrough), navaid_sim (boundary conversion), PositionPanel.jsx (remove DEG2RAD on IC).
- AFFECTED CONFIG: c172/ec135 config.yaml default_ic lat/lon now in degrees.
- NOTE: Attitude angles (roll_rad, pitch_rad, true_heading_rad, heading_rad) remain in radians. Separate decision pending.

## 2026-03-24 — 22:30:00 - Claude Code
- DECIDED: Freeze Position + Freeze Fuel toggles added as independent flags on SimState (bool freeze_position, bool freeze_fuel). CMD_FREEZE_POSITION=12, CMD_FREEZE_FUEL=13 toggle in sim_manager. Independent of FROZEN state machine — work only while RUNNING. CMD_RESET and CMD_REPOSITION clear both.
- DECIDED: Freeze position implementation: capture lat/lon/alt on rising edge, let JSBSim step() normally, write frozen position back via property tree after step() + zero velocities. JSBSim computes aero/engines each frame but position resets before publish.
- DECIDED: Freeze fuel implementation: gate fuel_node update() on freeze_fuel flag (same pattern as existing is_frozen_ gate). No fuel drain while active.
- REASON: Instructor needs independent position/fuel freeze for training scenarios — hold position while monitoring engine behavior, or fly without fuel penalty.
- AFFECTS: SimState.msg, SimCommand.msg, sim_manager, flight_model_adapter, fuel_node, ios_backend, ActionBar.jsx, useSimStore.js

## 2026-03-25 — Claude Code

### Architecture audit Batch 1 bug fixes

- FIXED: F2.1 — Removed dead FailureList subscriptions from 5 systems nodes (electrical, fuel, engines, gear, air_data). These subscribed to `/sim/failures/active` which was never published. Failure system works exclusively through FailureInjection routing topics. FailureList message type is now unused.
- FIXED: F2.3 — ios_backend virtual panel WS handler now routes to publish_virtual_panel() instead of publish_panel(). Virtual cockpit inputs no longer silently elevated to INSTRUCTOR priority.
- FIXED: F1.2 — sim_failures YAML path now uses ament_index_cpp::get_package_share_directory() instead of hardcoded relative path.
- FIXED: F4.1 — JSBSimAdapter gear failure clear path now sets pos-norm to 1.0 (fully extended) instead of -1.0 (invalid).
- FIXED: F4.3 — Moot: FailureList callbacks removed with F2.1.
- FIXED: F4.4 — False positive: null guard already present.
- FIXED: F1.1 — Added EngineSwitchConfig to IEnginesModel interface. C172 and EC135 plugins return switch IDs from engine.yaml config. engines_node reads switch IDs from plugin, no hardcoded strings in framework code.
- ALSO: Added COLCON_IGNORE to x-plane_plugins/ (requires cross-compilation, not part of workspace build).
- AFFECTS: electrical_node, fuel_node, engines_node, gear_node, air_data_node, failures_node, JSBSimAdapter, ios_backend, IEnginesModel interface, C172/EC135 engine plugins, engine.yaml configs.

## 2026-03-25 — Claude Code

### Architecture audit Batch 2 fixes

- FIXED: F3.6/F3.13 — Added reset() to IFuelModel interface. C172 + EC135 plugins restore tank quantities on reset. fuel_node calls model_->reset() on RESETTING.
- FIXED: F3.7 — navigation_node subscribes to /sim/state, clears DME HOLD state on RESETTING.
- FIXED: F1.3/F1.4 — Added jsbsim_model_name to config.yaml. JSBSimAdapter reads model name + default IC from config. No hardcoded aircraft-specific values in framework code.
- FIXED: F6.5 — useSimStore.js handles air_data_state WS messages, stores pitot-static data.
- FIXED: F1.14 — Created SearchNavaids.srv. navaid_sim provides search service (works with any data source). ios_backend calls service instead of maintaining its own XP-only parser.
- FALSE POSITIVE: F2.13 — navaid_sim already subscribes to /sim/failure/navaid_commands.
- AFFECTS: IFuelModel interface, fuel_node, C172/EC135 fuel plugins, navigation_node, JSBSimAdapter, config.yaml (both aircraft), navaid_sim, ios_backend, useSimStore.js, sim_msgs (new SearchNavaids.srv)

## 2026-03-25 — Claude Code

### Architecture audit Batch 3 fixes

- FIXED: EC135 missing air_data.yaml — created config (unblocks EC135 launch with sim_air_data).
- FIXED: F4.2 — cigi_bridge inet_aton replaced with inet_pton + error check + socket cleanup on failure.
- FIXED: F4.10 — atmosphere_node density now computed from actual OAT (not ISA temp). Correct density altitude in non-ISA conditions.
- FIXED: F4.7 — All 13 ios_backend ROS2 callbacks wrapped with @_safe_callback decorator. Errors logged instead of silently swallowed.
- FIXED: F3.1+F3.2 — flight_model_adapter and cigi_bridge now publish SimAlert on configure failure.
- FIXED: F3.3 — Added on_deactivate() call on LifecyclePublisher in 4 nodes (electrical, engines, gear, air_data).
- FIXED: F3.9 — All 13 lifecycle nodes check configure state before calling activate.
- FIXED: F3.12 — sim_navigation now has alert_pub_ for error reporting to IOS.
- AFFECTS: atmosphere_node, cigi_bridge, flight_model_adapter, ios_backend, sim_electrical, sim_engine_systems, sim_gear, sim_air_data, sim_navigation, all lifecycle nodes (configure guard), ec135 config.

## 2026-03-27 — Claude Code

### V3 unit suffix cleanup — _pct/_norm split

- DECIDED: All 0-1 ratio fields renamed from _pct to _norm. Convention locked: _pct = 0-100 (gauge %), _norm = 0-1 (internal ratio). No value changes — renames only.
- REASON: _pct suffix on 0-1 fields was misleading. Split convention eliminates ambiguity.
- RENAMED: FlightModelState (fuel_total_norm, gear_position_norm), FuelState (tank_quantity_norm, total_fuel_norm), GearState (position_norm), AirDataState (pitot_ice_norm), InitialConditions (fuel_total_norm)
- KEPT _pct: throttle_pct, flap_pct, speed_brake_pct (0-100), battery_soc_pct (0-100), n1_pct, n2_pct, torque_pct (0-100)
- ALSO: V3 batch 1-4 renames (FlightControls/EngineControls _norm, ElectricalState _v/_a, AvionicsControls obs_deg)
- AFFECTS: FlightModelState, FuelState, GearState, AirDataState, InitialConditions, config.yaml (both aircraft), all consumers (C++, Python, JS field name updates)

## 2026-03-27 — Claude Code

### Magnetic heading moved to air_data_node

- DECIDED: Removed magnetic_heading_rad from FlightModelState. Magnetic heading is a compass instrument output, not flight model truth. Now computed in air_data_node as true_heading - magnetic_variation.
- DECIDED: navaid_sim publishes /sim/world/magnetic_variation_deg (std_msgs/Float32, 1Hz) using existing WMM/MagDec class.
- DECIDED: AirDataState.msg gains magnetic_heading_rad + magnetic_variation_deg fields. IOS reads heading from AirDataState.
- REASON: FlightModelState is truth data. Magnetic heading is derived. This separation enables future compass failures (stuck DG, precession, deviation) in air_data_node without touching the FDM adapter.
- AFFECTS: FlightModelState.msg (field removed), AirDataState.msg (fields added), navaid_sim (new publisher), air_data_node (new subscription + computation), JSBSimAdapter (dead code removed), navigation_node (switched to true_heading_rad), ios_backend + frontend (heading source changed to airData)

## 2026-03-27 — Claude Code

### ArbitrationState + GearState forwarded to IOS, dead code cleanup, clock rate param

- DECIDED: ArbitrationState and GearState now forwarded to IOS via WebSocket. ios_backend subscribes to /sim/controls/arbitration and /sim/gear/state, forwards as arbitration_state and gear_state WS message types. Frontend stores wired but no display components yet.
- DECIDED: Dead code removed — caps_sub_ empty callback in cigi_bridge deleted, RepositionBase legacy files deleted.
- DECIDED: sim_manager clock_rate_hz parameter added (default 50.0). Replaces hardcoded 20ms timer.
- REASON: F2.10/F2.11 from architecture audit — data was published but never reached IOS. Dead code cleanup for git hygiene. Clock rate parameterization per CLAUDE.md documentation.
- AFFECTS: ios_backend (2 new subscriptions + WS handlers), useSimStore.js (2 new state slices), cigi_bridge (dead subscription + legacy files removed), sim_manager (parameterized clock rate)

## 2026-03-28 — 20:00 — Claude Code
- DECIDED: IOS A/C panel and cockpit panel render switches/CBs/loads dynamically from electrical.yaml. ios_backend loads config and sends as electrical_config WS message (same pattern as navigation.yaml → avionics_config).
- DECIDED: Per-switch FORCE arbitration replaces sticky per-channel instructor priority. Each switch tracks its own force state independently. FORCE checkbox on IOS A/C page per switch. Instructor forcing sw_battery does not lock out cockpit page sw_landing_lt.
- DECIDED: PanelControls.msg gains switch_forced[] and selector_forced[] arrays. Empty = normal command (cockpit/hardware). Populated = force/release (IOS).
- DECIDED: ArbitrationState.msg gains forced_switch_ids[] and forced_selector_ids[] for frontend force checkbox rendering.
- DECIDED: has_inst_panel_ sticky flag removed from arbitrator. Panel channel no longer has per-channel sticky behavior.
- REASON: Hardcoded switch panels require code changes when YAML changes. Per-channel sticky lockout made cockpit page unusable after any IOS interaction. Per-switch force matches real training sim behavior (instructor forces individual switches, not entire panel).
- AFFECTS: PanelControls.msg, ArbitrationState.msg, input_arbitrator (per-switch force model), ios_backend (electrical config loading), AircraftPanel.jsx (dynamic + FORCE), useSimStore.js (electricalConfig + force state)

## 2026-03-28 — Claude Code

### Electrical system fixes

- DECIDED: ElectricalSolver loads gate on switch_id field. LoadDef gains switch_id (empty = always-on). Solver checks panel_switch_states_ before including load current. Loads without switch_id behave as before.
- DECIDED: Starter routed through EngineCommands writeback. EngineCommands.msg gains starter_engage[4]. C172 engines plugin checks bus_voltage > 20V AND magneto START before setting starter_engage. FMA writes propulsion/starter_cmd from EngineCommands only. Direct starter_cmd write from EngineControls removed.
- DECIDED: JSBSimAdapter populates n1_pct as generic engine speed % for all engine types. Piston: propeller_rpm / max_rpm * 100. max_rpm from config.yaml. Fixes alternator RPM gate which read n1_pct (always 0 for pistons).
- DECIDED: updateRelayCoils() sets ss.closed = coil_powered (was only clearing on unpowered). commandSwitch() skips relay-type switches with coil_bus. Fixes essential_bus staying dead.
- DECIDED: Starter current updated to 150A nominal / 300A inrush. Initial SOC from YAML not hardcoded.
- AFFECTS: electrical_solver.hpp/cpp (LoadDef.switch_id, panel_switch_states_, relay logic), electrical.yaml (switch_id per load, starter 150A), EngineCommands.msg, engines_model.cpp (bus voltage gate + starter_engage), JSBSimAdapter (n1_pct, starter_cmd removal), flight_model_adapter_node (starter_cmd removal)

### Per-switch FORCE arbitration

- DECIDED: Replaced sticky has_inst_panel_ with per-switch force model. Each switch tracks forced state independently. PanelControls.msg gains switch_forced[] and selector_forced[]. ArbitrationState.msg gains forced_switch_ids[] and forced_selector_ids[].
- DECIDED: IOS A/C panel has FORCE checkbox per switch. Tick = force switch to value, untick = release to cockpit/hardware. Instructor commands without switch_forced no longer auto-force (backward compat clause removed).
- AFFECTS: PanelControls.msg, ArbitrationState.msg, input_arbitrator (per-switch merge), AircraftPanel.jsx (FORCE checkboxes), useSimStore.js

### Config-driven panels

- DECIDED: IOS A/C panel and C172 cockpit panel render switches dynamically from electrical.yaml. ios_backend loads config, sends as electrical_config WS message. Adding a switch to YAML auto-renders it on both pages.
- DECIDED: Both source switches (from switches section) and load switches (switch_id from loads section) rendered. Filtered by pilot_controllable for display.
- AFFECTS: ios_backend (electrical config loading), AircraftPanel.jsx, CockpitElectrical.jsx, useSimStore.js

### Performance

- DECIDED: All React components use useShallow selectors. Prevents re-render cascades from unrelated store updates.
- DECIDED: Track array capped at 10K points with thinning. Leaflet icon quantized to 2° heading. Backend throttles WS sends to 10Hz per topic. sim_time_sec rounded to reduce JSON diff churn.
- AFFECTS: All frontend components, useSimStore.js, ios_backend sender loop

### Engine RPM deduplication

- DECIDED: max_engine_rpm removed from config.yaml. FMA now reads rpm_max from engine.yaml (engines[0]). Single source of truth for engine RPM limit. Aircraft without engine.yaml fall back to JSBSimAdapter default (2700).
- AFFECTS: flight_model_adapter_node.cpp, c172 config.yaml, engine.yaml

## 2026-03-30 — Claude Chat

### Electrical solver v2: graph-based topology redesign

- DECIDED: Electrical topology redesigned as a graph model. Two core sections:
  `nodes` (sources, buses, junctions, loads) and `connections` (series elements
  between nodes). Replaces the previous sources/buses/switches/loads split with
  inline CBs and switch_id references.
- REASON: Previous model had CBs embedded inside LoadDef with no command path
  from IOS. Switch/CB/relay used separate code paths. Bus-level CBs (needed for
  EC135 twin-generator topology) were not expressible. Graph model unifies all
  series elements as connections with from→to, one solver walk.

- DECIDED: Connection types: wire, switch, contactor, relay (coil_bus),
  circuit_breaker (rating + auto-trip), potentiometer (analog 0-1), selector
  (multi-position with per-position closes[] and spring_return map). Internally
  normalized to common ConnectionState struct.
- REASON: Covers C172 plus future needs (magneto selector, MOM-OFF-MOM trim,
  panel dimming). Selector uses positions[].closes[] for multi-output routing —
  "a bunch of switches in a rotary package."

- DECIDED: Junction nodes represent net names (wires between CB and switch in
  series load chains). Explicit in YAML, named jct_<load_id>.
- REASON: Makes schematic-to-YAML translation mechanical. Every wire segment
  between two component pins gets a name. Enables future graphical schematic editor.

- DECIDED: panel_layout removed from electrical topology. Future separate config.
- REASON: Layout is a rendering concern, not topology.

- DECIDED: Removed unused fields from v2 schema: affected_systems (nothing
  consumes), max_current on sources (no overload model), essential on loads
  (no load shedding), contactor_delay_ms (switching instant in solver).
- REASON: Dead data. Re-add when solver features exist to consume them.

- DECIDED: Failure effects model — failures are property overrides on graph
  elements, not custom-coded behaviors. Four actions: force (override value),
  jam (force + ignore commands), disable (turn off solver behavior like
  overcurrent_trip), multiply (scale numeric property). Each failure in
  failures.yaml lists one or more effects with target/action/property/value.
  No C++ per failure type.
- REASON: Adding new electrical failures becomes pure YAML authoring. Solver
  just applies active overrides before computing each element.

- DECIDED: Three-phase implementation: (1) v2 YAML schema for C172 [done].
  (2) Standalone graph solver with unit tests — no ROS2 dependency [done].
  (3) Swap into live system.
- AFFECTS: electrical_solver.hpp/cpp (full rewrite), electrical_v2.yaml (new
  format), C172/EC135 electrical_model plugins, ElectricalState.msg (snapshot),
  IOS electrical config forwarding, AircraftPanel.jsx CB rendering,
  failures.yaml (effects model).

## 2026-03-30 — 22:40:57 - Claude Code
- DECIDED: GraphSolver implemented as standalone C++ library in sim_electrical
  package, namespace `elec_graph` (separate from old `elec_sys`). No ROS2
  dependency. Loads electrical_v2.yaml graph topology.
- REASON: Phase 2 of electrical solver v2. Old solver stays untouched until
  phase 3 swap. Graph solver uses BFS propagation from powered sources through
  closed connections — cleaner and more correct than old flat iteration.
- AFFECTS: graph_types.hpp, graph_solver.hpp, graph_solver.cpp,
  test_graph_solver.cpp, CMakeLists.txt (sim_electrical)

- DECIDED: Graph solver uses multi-pass BFS with full node reset between passes.
  Each pass: reset non-source nodes → BFS from online sources → update relay
  coils. propagation_passes=4 (configurable in YAML). Handles relay cascades
  correctly — relay coil state re-evaluated after each BFS pass.
- REASON: Old solver did not reset between passes, which could leave stale
  power states when a relay opens. BFS is O(V+E) per pass, fast for small graphs.

- DECIDED: Failure effects stored as map<(target_id, property), value>.
  applyFailureEffect(target, action, property, value) → stores override.
  updateSources checks "online" override. updateLoads checks "current_multiplier".
  commandConnection checks jammed state. Jam captures current closed position.
- REASON: Property-level overrides are more flexible than the old solver's
  simple target→fault_type string map. New failures become YAML-only additions.

- DECIDED: Node count for C172 v2 topology is 39 (3 sources + 4 buses +
  11 junctions + 21 loads), 38 connections. Task card said 40 — off by one,
  actual YAML parse yields 39.
- AFFECTS: test_graph_solver T1 assertion

- DECIDED: CB model simplified — no automatic overcurrent trip, no thermal
  model, no inrush physics. CBs behave as switches with tripped state
  settable only by failure effects or commands. Loads draw flat
  nominal_current when powered. load_type, inrush_current,
  inrush_duration_ms removed from YAML.
- REASON: Overcurrent physics introduced complex interactions (inrush vs
  thermal trip timing, resistive V/R scaling) that added no training value.
  CB state is driven by the failure injection system, not the solver.
- AFFECTS: graph_types.hpp (LoadParams simplified), graph_solver.cpp
  (updateLoads is 10 lines), electrical.yaml (loads carry only
  nominal_current), test_graph_solver.cpp (T8 tests failure-driven trip)

## 2026-03-30 — 23:54:35 - Claude Code
- DECIDED: Phase 3a complete — C172 electrical_model.cpp swapped from
  ElectricalSolver (elec_sys) to GraphSolver (elec_graph). EC135 stays
  on old solver.
- REASON: Graph solver is tested (15/15), simpler, and the v2 YAML captures
  the C172 schematic more faithfully (CBs are connections, not embedded in loads).
- AFFECTS: aircraft_c172/electrical_model.cpp, electrical.yaml (renamed from
  electrical_v2.yaml), electrical_v1_backup.yaml (old format preserved)

- DECIDED: Failure translation in C172 ElectricalModel: apply_failure()
  detects whether target is a node (→ force online=false) or connection
  (→ force tripped=true). Clear removes all known overrides for target.
  No interface changes to IElectricalModel.
- REASON: electrical_node.cpp always sends "component_id/fail" format.
  Translation layer in the plugin keeps the node generic.

## 2026-03-31 — Claude Chat + Claude Code

### Electrical solver v2: graph-based topology redesign + live swap

- DECIDED: Electrical topology redesigned as a graph model. Two core sections:
  `nodes` (sources, buses, junctions, loads) and `connections` (series elements
  between nodes). Replaces the previous sources/buses/switches/loads split with
  inline CBs and switch_id references. C172 topology: 39 nodes, 38 connections.
- REASON: Previous model had CBs embedded inside LoadDef with no command path
  from IOS. Switch/CB/relay used separate code paths. Bus-level CBs (needed for
  EC135 twin-generator topology) were not expressible. Graph model unifies all
  series elements as connections with from→to, one solver walk.

- DECIDED: Connection types: wire, switch, contactor, relay (coil_bus),
  circuit_breaker (rating, instructor-trip only), potentiometer (analog 0-1,
  not yet implemented), selector (multi-position with per-position closes[] and
  spring_return map, not yet implemented). Internally normalized to common
  ConnectionState struct.
- REASON: Covers C172 plus future needs (magneto selector, MOM-OFF-MOM trim,
  panel dimming). Selector uses positions[].closes[] for multi-output routing.

- DECIDED: Junction nodes represent net names (wires between CB and switch in
  series load chains). Explicit in YAML, named jct_<load_id>. Only needed where
  a load has both a CB and a pilot switch in series.
- REASON: Makes schematic-to-YAML translation mechanical. Every wire segment
  between two component pins gets a name. Enables future graphical schematic editor.

- DECIDED: CB overcurrent physics removed entirely. No thermal model, no inrush,
  no automatic trip. CBs are switches that only trip via failure effects or IOS
  commands. Loads draw flat nominal_current when powered.
- REASON: In training sim, CBs only trip when instructor says so. Overcurrent
  physics caused false trips from YAML typos (hard to debug with thermal
  accumulation) and inrush timers break in freeze mode (dt=0). No FNPT II
  training scenario requires automatic CB trip.

- DECIDED: Removed unused YAML fields from v2 schema: affected_systems (nothing
  consumes), max_current on sources (no overload model), essential on loads (no
  load shedding), contactor_delay_ms (switching instant), inrush_current,
  inrush_duration_ms, load_type (no behavioral difference without inrush/resistive
  scaling). Load nodes carry: id, type, label, nominal_current.
- REASON: Dead data confirmed by code audit. Re-add when features exist.

- DECIDED: panel_layout removed from electrical topology. Future separate config
  file (panels.yaml) will define IOS and cockpit panel layouts.
- REASON: Layout is a rendering concern, not topology. Decouples panel
  rearrangement from electrical behavior changes.

- DECIDED: Failure effects model — failures are property overrides on graph
  elements. Four generic actions: force (override value), jam (force + ignore
  commands), disable (turn off solver behavior), multiply (scale numeric property).
  failures.yaml references graph element IDs directly. No per-failure C++ code.
- REASON: Adding new electrical failures is pure YAML authoring. Solver applies
  active overrides before computing each element.

- DECIDED: commandConnection() does not gate on pilot_controllable. That field is
  metadata for the frontend (which controls to render as interactive). Solver
  accepts all commands unconditionally.
- REASON: sw_starter_engage has pilot_controllable: false (controlled by engines
  plugin). Gating in solver prevented engine start.

- DECIDED: Alternator min_rpm_pct lowered from 30% to 20%. Starter voltage
  threshold lowered from 20V to 14V.
- REASON: At idle (~650 RPM), n1_pct ≈ 24%. 30% threshold required throttle for
  alternator. Battery sags to ~17V under 150A starter load — 20V prevented crank.

- DECIDED: CBs interactive on IOS and virtual cockpit. Same command path as
  switches — cb_ IDs flow through switch_ids[] in PanelControls. No message
  changes. IOS: 3-column grid with FORCE checkbox, 3-state (IN/POPPED/LOCKED).
  Virtual cockpit: horizontal CB row with colored dots.
- REASON: Completes EURAMEC 3-state model. Unified command path, no new plumbing.

- DECIDED: ios_backend electrical config parser updated for v2 YAML format.
  Reads nodes + connections instead of old format sections.
- REASON: v2 YAML has no switches: or loads: top-level keys. Old parser returned
  empty config, IOS showed no controls.

- DECIDED: Three-phase implementation completed:
  Phase 1: v2 YAML schema for C172 (39 nodes, 38 connections) [done]
  Phase 2: Standalone graph solver, 15 unit tests, no ROS2 dependency [done]
  Phase 3: Live swap — C172 plugin, ios_backend parser, interactive CBs [done]
- AFFECTS: graph_types.hpp, graph_solver.hpp/cpp (new), electrical_v2.yaml→
  electrical.yaml, c172/electrical_model.cpp, ios_backend_node.py,
  AircraftPanel.jsx, C172Panel.jsx, failures_node.cpp,
  c172/engines_model.cpp. Old solver files kept for EC135.

## 2026-04-03 — Claude Code

- DECIDED: Migrate entire topic namespace from two roots (/devices/ + /sim/) to three roots
  (/world/ + /aircraft/ + /sim/). Design originated in Claude Chat session same day.
  /world/ = environment (weather, navaids, terrain), /aircraft/ = the simulated machine
  (FDM, systems, controls, input devices), /sim/ = simulation infrastructure (state machine,
  diagnostics, CIGI, failures, scenarios).
- REASON: Previous two-root model was an engineering boundary, not a conceptual model. Three
  roots map to how instructors, certification auditors, and new developers think. SimSnapshot
  sections map directly (world, aircraft, sim).
- AFFECTS: 17 C++ node files, ios_backend_node.py, 3 frontend files, CLAUDE.md, DECISIONS.md,
  agent files. All topic strings migrated. No .msg changes, no logic changes.

- DECIDED: Input devices nest under /aircraft/devices/ instead of root /devices/.
- DECIDED: /sim/failure/ topics renamed to /sim/failures/route/<handler>.
- DECIDED: /sim/failure_state renamed to /sim/failures/state.
- DECIDED: /sim/diagnostics/lifecycle_state shortened to /sim/diagnostics/lifecycle.
- DECIDED: /sim/flight_model/ shortened to /aircraft/fdm/.
- DECIDED: /sim/world/* moved to /world/*.
- DECIDED: /clock stays at /clock (ROS2 convention).

## 2026-04-04 — Claude Chat

- DECIDED: Redesign sim_fuel around a graph topology model, same architectural pattern as
  the electrical GraphSolver. Two-layer solver: Layer 1 (topological) is mandatory, Layer 2
  (flow physics) is optional per aircraft.

  LAYER 1 — TOPOLOGICAL (binary reachability):
  BFS from tanks through open connections to engine inlets. A path exists or it doesn't.
  Pumps are nodes that must be running for fuel to pass through. Valves/selectors are
  connections controlled by panel switches. If an engine inlet is reachable from a tank
  with usable fuel through at least one running pump → engine is fed. Otherwise →
  starvation. Fuel consumption drained from connected tanks, split equally or by priority.

  Fixes current bugs:
  - Fuel valve OFF → selector closes all paths → no route → starve
  - Both pumps fail → no pump node passable → starvation
  - Fuel leak → connection drains at leak_rate_lph from upstream tank

  LAYER 2 — FLOW PHYSICS (optional, per aircraft):
  Activated when any tank node has position_m defined. Computes gravity head pressure from
  fuel level, tank body-frame position, and attitude/accelerations from FlightModelState:
    head_pressure = density × g_effective × height_difference
  Flow rate proportional to pressure differential, capped by connection max_flow_lph. One
  tuning parameter per connection: flow_resistance (default 1.0). Only needed for
  crossfeed/asymmetric scenarios (C208 sideslip, DA42 crossfeed). C172 uses Layer 1 only.

- REASON: Current flat solver has no concept of fuel flow path. "Fuel valve OFF" means
  "stop draining tanks" but never starves the engine. Boost pump failure is cosmetic.
  These are simulation errors. Graph topology makes correct behavior emerge from structure,
  same as electrical.

- DECIDED: Fuel graph solver node types:
  tank          — quantity_kg, capacity_kg, unusable_kg, arm_m, optional position_m
  pump          — source: engine (RPM-driven) or electrical (bus-powered via switch_id).
                  Pump is a NODE (not connection) — has own state, visible in future
                  schematic view
  junction      — passive merge/split (strainer, manifold, tee)
  engine_inlet  — engine_index, reports fed/starved

- DECIDED: Fuel graph solver connection types:
  line          — passive pipe, default open
  valve         — commandable open/close via switch_id
  selector      — group-based multi-position (see selector design below)
  check_valve   — one-way flow only

- DECIDED: Selector design — named positions with group pattern. Selectors use the same
  approach for both fuel and electrical:

  Top-level `selectors:` section defines the physical switch:
    id, switch_id, positions (named list), default_position, optional spring_return map

  Connections reference a selector group and list which named positions they are open in
  via `open_in: [POS1, POS2]`. Each physical path through the selector is its own
  connection with single from/to (consistent with electrical solver — no multi-input
  connections). The group ties them to one physical switch.

  Example — C172 fuel selector:
    selectors:
      - id: sel_fuel
        switch_id: sel_fuel_selector
        positions: [BOTH, LEFT, RIGHT, OFF]
        default_position: BOTH
    connections:
      - id: sel_fuel_left
        type: selector
        from: tank_left
        to: fuel_strainer
        selector: { group: sel_fuel, open_in: [BOTH, LEFT] }
      - id: sel_fuel_right
        type: selector
        from: tank_right
        to: fuel_strainer
        selector: { group: sel_fuel, open_in: [BOTH, RIGHT] }

  YAML reads like English: "left tank to strainer is open in BOTH and LEFT positions."
  No magic numbers. Solver maps position names to index internally.

  Pattern extends to DA42 crossfeed (open_in: [OPEN]), electrical magneto selector
  (positions: [OFF, R, L, BOTH, START] with spring_return: {START: BOTH}), and any future
  multi-position switch. This becomes the shared selector pattern for BOTH fuel and
  electrical solvers.

- DECIDED: Failure effects use identical model to electrical: force, jam, disable, multiply.
  YAML-only failure authoring. Examples:
  - Mechanical pump fail: force pump_mechanical.online = false
  - Fuel line leak: set line_strainer.leak_rate_lph = 10.0
  - Fuel valve stuck: jam sel_fuel_left (captures position)
  - Fuel filter blocked: force line_filter.closed = true

- DECIDED: JSBSim owns weight and balance. Fuel solver owns tank contents. Writeback:
  solver quantities → FuelState.msg → JSBSim propulsion/tank[N]/contents-lbs → JSBSim
  recalculates CG automatically. We never compute W&B.

- DECIDED: FuelFdmInputs struct:
  engine_rpm_pct[], engine_fuel_demand_kgs[], on_ground, pitch_rad, roll_rad,
  accel_x, accel_y, accel_z

- DECIDED: C172 fuel topology: 7 nodes (2 tanks, 2 junctions, 2 pumps, 1 engine inlet),
  6 connections + 2 selector connections. Layer 1 only (no tank position_m). 1 selector
  group (4 positions: BOTH/LEFT/RIGHT/OFF).

- DECIDED: Three-phase implementation:
  Phase 1: YAML schema + C172 fuel topology YAML + DECISIONS.md
  Phase 2: Standalone FuelGraphSolver (namespace fuel_graph), unit tests, no ROS2
  Phase 3: Live swap into fuel_node via C172 plugin

- DECIDED: EC135 fuel plugin stays on current flat solver (symmetric tanks, no crossfeed,
  no selector — flat solver is correct). Migration to graph solver optional.

- AFFECTS: fuel_graph_types.hpp, fuel_graph_solver.hpp/cpp (new),
  test_fuel_graph_solver.cpp (new), aircraft/c172/config/fuel.yaml (rewrite),
  aircraft/c172/src/fuel_model.cpp (rewrite), IFuelModel interface (unchanged),
  fuel_node.cpp (unchanged), FuelState.msg (unchanged),
  ios_backend (fuel_config parser v2), failures.yaml (new fuel failure entries)

## 2026-04-04 — 12:12:39 - Claude Code

- DECIDED: Implemented FuelGraphSolver Phase 2 — standalone solver library (namespace
  fuel_graph) with 15 unit tests. No ROS2 dependency. Mirrors electrical GraphSolver
  patterns: same failure effect API, same YAML loading, same test macros.
- REASON: Phase 2 of three-phase fuel graph solver implementation. Validates topology
  BFS, selector group pattern, pump-as-node model, leak tracing, and CAS evaluation
  before integrating into the live fuel_node.
- AFFECTS: fuel_graph_types.hpp, fuel_graph_solver.hpp, fuel_graph_solver.cpp (new),
  test_fuel_graph_solver.cpp + c172_fuel_v2.yaml test fixture (new),
  sim_fuel/CMakeLists.txt (fuel_graph_solver library + test executable added).
  No changes to fuel_node.cpp, IFuelModel, plugins, or messages.

## 2026-04-04 — 12:23:48 - Claude Code

- DECIDED: Implemented FuelGraphSolver Phase 3 — C172 fuel plugin swapped to graph solver.
  Fuel valve OFF now causes engine starvation. Pump failures cause starvation unless backup
  electric pump engaged. Old flat solver preserved as fuel_v1_backup.yaml.
- DECIDED: Engine RPM for mechanical pump gate uses simplification: fuel_flow > 0 → RPM=100%,
  flow=0 → RPM=0%. Correct for C172 Layer 1 topological model (pump is either running or not).
  Proper RPM passthrough deferred to avoid IFuelModel interface change.
- DECIDED: Writeback starvation path — when engine demands fuel but fuel_pressure_pa is zero
  (engine starved), writeback zeroes all tank quantities to JSBSim. JSBSim's engine naturally
  quits from empty tanks. Display FuelState keeps real tank quantities. When valve reopens,
  writeback restores real quantities and JSBSim restarts fuel flow.
- DECIDED: fuel.yaml v2 keeps backward-compat `fuel.tanks` shim for fuel_node.cpp which reads
  tank_count from that array. Tank topology data lives in top-level `nodes:` section.
- DECIDED: ios_backend fuel config parser detects v1/v2 by presence of top-level `nodes` key.
  v2 parser extracts tank nodes, pump nodes, and selector definitions for IOS display.
- REASON: Phase 3 of three-phase fuel graph solver implementation. Completes the design goal:
  fuel valve OFF kills engine, pump failures cause starvation, all behavior emerges from
  graph topology.
- AFFECTS: aircraft/c172/src/fuel_model.cpp (rewritten), aircraft/c172/config/fuel.yaml
  (v2 graph format), aircraft/c172/CMakeLists.txt (sim_fuel dependency), fuel_node.cpp
  (writeback starvation logic), fuel_graph_solver.hpp/cpp (setTankQuantity added),
  ios_backend_node.py (v2 fuel config parser). IFuelModel interface unchanged.

## 2026-04-04 — 14:20:00 - Claude Code

### Architecture audit batch: F4.1, F3.6, F3.3, F3.8, F4.10

- DECIDED: All 5 findings already resolved in current code. No code changes needed.
  - F4.1: JSBSimAdapter gear clear pos-norm already 1.0 (not -1.0)
  - F3.6: fuel_node already calls model_->reset() on RESETTING
  - F3.3: All 4 nodes already have state_pub_->on_deactivate()
  - F3.8: Electrical solver runs in INIT/READY by design (cockpit needs bus power before CMD_RUN)
  - F4.10: Atmosphere density already uses oat_k (ISA + deviation), not ISA baseline
- DECIDED: F3.7 (DME HOLD reset on RESETTING) is NOT a bug — deferred as design debt. DME HOLD
  is an avionics-device-specific feature (e.g., KNS-80), not universal receiver behavior.
  navigation_node is the aircraft-agnostic receiver layer. When avionics plugin layer is built,
  DME HOLD state will migrate there.
- REASON: Architecture audit findings reviewed against current code. All were either fixed during
  prior implementation or are intentional design choices.
- AFFECTS: bugs.md updated. No source code changes.

## 2026-04-04 — Claude Chat

### State inspector panel design (Phase 1 read-only)

- DECIDED: Add "DBG" tab to IOS nav column. InspectorPanel.jsx renders a live collapsible
  tree of all WebSocket state messages from useSimStore. Search/filter at top. No backend
  changes — pure frontend read.
- DECIDED: Phase 2 (not built): DebugOverride.msg for admin-level value overrides from IOS.
  Topic /sim/debug/override. Nodes opt in by subscribing and mapping property_path to existing
  internal override APIs (electrical applyFailureEffect, fuel setTankQuantity, FMA set_property).
  Inspector UI shows editable fields with amber highlight on active overrides.
- REASON: Primary debug tool for daily development and QTG tuning. Replaces terminal
  ros2 topic echo. Phase 2 deferred until use cases prove which values need editing most.
- AFFECTS: NavTabs.jsx (new tab), SidePanel.jsx (new mapping), InspectorPanel.jsx (new
  component). No backend changes.

## 2026-04-06 — Claude Code

### Generic topic forwarder (Phase 2a backend)

- DECIDED: TopicForwarder class in ios_backend dynamically discovers and subscribes to all
  active ROS2 topics with known message types. Forwards raw SI values (no conversion) as
  `topic_tree` and `topic_update` WS messages alongside existing hand-coded callbacks.
  Existing IOS panels continue working unchanged — forwarder runs in parallel.
- DECIDED: 5 Hz throttle per topic to prevent WS flooding. topic_tree metadata sent every 3s.
  topic_update values sent every 200ms. Own published topics excluded (no echo).
  Transient-local QoS for capabilities topics. Stale subscription cleanup on node death.
- DECIDED: message_to_ordereddict (rosidl_runtime_py) for generic serialization.
  _deep_convert handles numpy→native Python for JSON. Known types built from sim_msgs.msg
  introspection + std_msgs + rosgraph_msgs.
- REASON: Enables Phase 2b frontend (InspectorPanel) to show ALL topics with raw values,
  not just the subset forwarded by hand-coded callbacks. Replaces ros2 topic echo for
  real-time debugging.
- AFFECTS: topic_forwarder.py (new), ios_backend_node.py (import + instantiate forwarder,
  discovery in refresh_graph, topic_tree/topic_update in sender loop).

## 2026-04-06 — Claude Code

### Topic tree inspector frontend (Phase 2b)

- DECIDED: InspectorPanel rewritten from flat 9-group store view to full ROS2 namespace tree.
  Consumes topic_tree and topic_update WS messages from TopicForwarder. Tree organized by
  three-root namespace (/world/, /aircraft/, /sim/) plus /clock. Topics show message type
  badge and green/grey data indicator dot.
- DECIDED: useSimStore gains topicTree and topicValues state fields, with handlers for the
  two new WS message types. Independent of existing state — no interaction with old panels.
- REASON: Phase 2b of generic topic forwarder. Inspector now shows all active ROS2 topics
  with raw SI values, not just the hand-coded subset.
- AFFECTS: useSimStore.js (new state + handlers), InspectorPanel.jsx (rewritten).
  No backend changes, no other panel changes.

## 2026-04-10 — Claude Chat

- DECIDED: AircraftPanel split into config-driven horizontal sub-tabs:
  Fuel & W/B (default) | Electrical | Engines | Radios. Each tab is a
  separate .jsx file under `panels/aircraft/`. Tabs only render when
  their corresponding config is non-null in the store.
- REASON: AircraftPanel grew too large as a single component. Sub-tabs
  match instructor workflow (fuel/weight setup first, then electrical).
  Config-driven visibility ensures framework extensibility — future
  aircraft systems (hydraulic, pressurization, ice protection) just add
  a new tab component and config, no restructuring needed.
- AFFECTS: AircraftPanel.jsx (thin shell with tab bar),
  panels/aircraft/FuelTab.jsx, panels/aircraft/ElectricalTab.jsx,
  panels/aircraft/EnginesTab.jsx, panels/aircraft/RadiosTab.jsx

### Weight & Balance — Fuel & W/B Tab Design

- DECIDED: X-Plane style bottom-up loading approach for Weight & Balance.
  Individual payload station sliders + per-tank fuel sliders. CG is a
  consequence of loading, not a direct input. CG envelope chart shows
  the resulting dot (read-only, plotted from JSBSim output). No EURAMEC-style
  CG drag — it introduces back-calculation ambiguity and fights JSBSim's
  mass model.
- REASON: Maps 1:1 to JSBSim's pointmass model. Each IOS slider corresponds
  to one JSBSim pointmass or fuel tank property. No inverse kinematics needed.
  More realistic for training — instructors load actual stations (pilot,
  copilot, pax, baggage), CG falls where physics says.

- DECIDED: New aircraft config file `weight.yaml` per aircraft, defining:
  empty_weight_lbs, empty_cg_arm_in, payload_stations[] (index, label, arm_in,
  default_lbs, max_lbs), fuel_stations[] (tank_index, label, arm_in,
  capacity_lbs, default_lbs), cg_envelope (forward[] and aft[] polylines),
  max_ramp/takeoff/landing_weight_lbs, unit_label.
- REASON: Config-driven — new aircraft just adds weight.yaml. Framework code
  is aircraft-agnostic. CG envelope chart data comes from this file.

- DECIDED: JSBSim adapter reads CG from `inertia/cg-x-in` and total weight
  from `inertia/weight-lbs`. New FlightModelState.msg fields: float32 cg_x_in,
  float32 cg_y_in. Other adapters populate from their own mass balance outputs.
- REASON: JSBSim owns CG calculation. Framework just reads and displays.

- DECIDED: Payload commands via WS `set_payload` → `/aircraft/payload/command`
  (PayloadCommand.msg). JSBSimAdapter subscribes, writes
  `inertia/pointmass-weight-lbs[N]`. Set once on scenario setup, not per-frame.
- REASON: Payload changes are infrequent instructor actions, not continuous state.

- DECIDED: Fuel loading via WS `set_fuel_loading` → fuel solver (not direct to
  JSBSim). Fuel solver updates tanks, writeback pushes to JSBSim.
- REASON: Fuel quantities must flow through fuel solver (starvation logic,
  consumption tracking). Direct JSBSim writes would create state divergence.

- DECIDED: Fuel sliders show lbs primary for US-type aircraft (C172, C208).
  Unit toggle switches primary/secondary (lbs/kg). Gallons/liters as read-only
  reference. Revised from earlier kg-primary decision.
- REASON: POH, W&B charts, and JSBSim all use lbs. Unit toggle covers both
  EASA (kg) and FAA (lbs) preferences.

- DECIDED: CG envelope chart as SVG in FuelTab. X-axis: arm (in), Y-axis:
  weight (lbs). Envelope polygon from weight.yaml polylines. Live CG dot
  from FlightModelState (green inside, red outside). Reference lines for
  max weights. Updates live as sliders move.
- REASON: SVG renders crisp, data is simple, matches POH weight-vs-arm format.

- DECIDED: "Total Payload" and "Total Internal Fuel" master sliders scale
  individual stations proportionally. Individual sliders still adjustable.
  "Restore Defaults" resets to weight.yaml defaults. Changes apply immediately.
- REASON: Convenience for quick heavy/light scenarios. Immediate apply matches
  existing IOS pattern.

- DECIDED: Flight time estimate: total_fuel / cruise_fuel_flow, displayed as
  HH:MM. Cruise fuel flow from engine.yaml (new optional cruise_fuel_flow_gph).
  Frontend-only calculation.

- DECIDED: Implementation phases:
  Phase 1: weight.yaml + FlightModelState CG fields + JSBSimAdapter + backend
  Phase 2: FuelTab UI — sliders, summary, WS commands, adapter writes
  Phase 3: CG envelope SVG chart
  Phase 4: Flight time estimate
- AFFECTS: FlightModelState.msg (cg_x_in, cg_y_in), JSBSimAdapter.cpp,
  ios_backend_node.py, weight.yaml (new), FuelTab.jsx, useSimStore.js

## 2026-04-10 — Claude Chat + Claude Code

### Weight & Balance — Full Implementation

- DECIDED: X-Plane style bottom-up loading for Weight & Balance. Individual
  payload station sliders + per-tank fuel sliders. CG is a consequence of
  loading, not a direct input. Rejected EURAMEC top-down approach (set CG
  directly) — it fights JSBSim's pointmass model.
- REASON: Each IOS slider maps 1:1 to a JSBSim pointmass or fuel tank
  property. No inverse kinematics needed. Matches real W&B sheets.

- DECIDED: weight.yaml per aircraft with payload_stations, fuel_stations,
  cg_envelope polylines, weight limits, empty weight/CG. Arms must match
  JSBSim XML pointmass order.

- DECIDED: FlightModelState.msg extended with cg_x_in and cg_y_in.
  JSBSimAdapter reads inertia/cg-x-in and inertia/cg-y-in.

- DECIDED: Payload commands: set_payload WS → PayloadCommand.msg on
  /aircraft/payload/command → JSBSimAdapter writes pointmass-weight-lbs[N].

- DECIDED: Fuel loading: set_fuel_loading WS → PayloadCommand.msg on
  /aircraft/fuel/load_command → fuel_node → model_->set_tank_quantity() →
  solver internal state → writeback → JSBSim. Fuel solver is authority.
- REASON: Direct JSBSim writes (Bug #13a) caused solver state divergence.
  InitialConditions path (Bug #13) triggered RunIC reset. Correct path goes
  through solver. IFuelModel::set_tank_quantity() added to interface.

- DECIDED: FuelTab layout: left column sliders (payload + fuel + masters +
  Restore Defaults), right column summary (weight donut, CG readout, CG
  envelope SVG chart, IN/OUT ENVELOPE status).

- DECIDED: Fuel slider sync: fuelDirty flag with 2s cooldown prevents
  solver state from fighting user input during drag.

- DECIDED: Weight donut uses locally-computed total for instant feedback.
  FDM confirmed weight as secondary readout.

- DECIDED: CG envelope SVG chart: POH-standard orientation, envelope polygon
  from weight.yaml, live CG dot (green/red), EW marker, ZFW diamond,
  loading line, MTOW/MLW dashes, lbs/kg toggle.

- DECIDED: Inside-envelope test: interpolate forward/aft arm limits at
  current weight from envelope polylines.

- AFFECTS: FlightModelState.msg (cg_x_in, cg_y_in), PayloadCommand.msg
  (new), i_fuel_model.hpp (set_tank_quantity), JSBSimAdapter.cpp,
  fuel_node.cpp, ios_backend_node.py, useSimStore.js, FuelTab.jsx,
  weight.yaml (new per aircraft)

### Bug fixes — #10, #11, #12, #13

- BUG #10: Forced switch remembered pilot click after unlock. Fixed at 3
  layers: frontend click guard, arbitrator discard during force, copy
  force_value on release.

- BUG #11: CB only popped when switch toggled afterward. First-ever input
  matched uninitialized default — change detection missed it. Fixed: set
  changed=true when has_virtual/has_hardware transitions false→true.

- BUG #12 (a-f): Battery charging chain. (a) Multi-hop BFS for charge path.
  (b) Charging voltage minus IR drop. (c) Previous-frame detection avoids
  updateSources overwrite (reverted NodeState approach — ABI mismatch).
  (d) Segfault guards on detectChargingVoltage. (e) ABI mismatch from
  header member — removed, use local variable. (f) Removed discharge/charge
  if/else split — net current (charge minus drain) instead.

- BUG #13 (a-b): Fuel slider reset. (a) Replaced InitialConditions path
  with direct /aircraft/fuel/load_command. (b) Moved subscription from
  adapter to fuel_node — solver authority preserved via set_tank_quantity.
## 2026-04-16 — Claude Code
- DECIDED: Aircraft-specific electrical tests moved from sim_electrical
  to aircraft_c172 package. sim_electrical/test/ contains only generic
  solver tests (YAML validation + synthetic topology fixtures).
- REASON: sim_electrical is framework code and should not carry C172
  knowledge. Pattern extends cleanly to future aircraft packages.
- AFFECTS: src/systems/electrical/test/test_graph_solver.cpp (trimmed),
  src/systems/electrical/CMakeLists.txt (compile def removed),
  src/aircraft/c172/test/test_c172_electrical.cpp (new),
  src/aircraft/c172/CMakeLists.txt (new test target)

## 2026-04-16 — Claude Code
- DECIDED: C172-specific fuel tests moved from sim_fuel to aircraft_c172
  package. sim_fuel/test/ now contains only generic solver tests using
  synthetic inline topology. Matches the sim_electrical split.
- REASON: sim_fuel is framework code and should not carry C172 knowledge.
  Pattern extends cleanly to future aircraft packages.
- AFFECTS: src/systems/fuel/test/test_fuel_graph_solver.cpp (rewritten
  with synthetic topology), src/systems/fuel/CMakeLists.txt (compile def
  removed), src/aircraft/c172/test/test_c172_fuel.cpp (new, 15 tests),
  src/aircraft/c172/test/c172_fuel_v2.yaml (moved from sim_fuel),
  src/aircraft/c172/CMakeLists.txt (new test target).

## 2026-04-16 — Claude Chat (Weather Architecture Design Session)

- DECIDED: **WeatherState.msg replaced in-place** — old flat single-layer message replaced with
  layered CIGI-aligned structure. Same topic `/world/weather`, same package sim_msgs.
  New structure: global atmosphere scalars + dynamic CloudLayer[] (max 3) + dynamic WindLayer[]
  (max 13) + precipitation + surface conditions + FSTD control + turbulence model selector +
  MicroburstHazard[] array. Units: SI on wire (m, m/s, Pa, K, deg). No mixed units.
- REASON: Old WeatherState had dead wind pipeline (nothing wrote wind into JSBSim), mixed units
  (kts, ft, deg, m, K, Pa), single-layer model that couldn't express the layered weather both
  CIGI 3.3 and X-Plane 12 expect. New structure mirrors IOS/X-Plane mental model (separate
  cloud and wind layers) while mapping cleanly to CIGI 0x0C Weather Control packets via
  cigi_bridge composition.
- AFFECTS: sim_msgs/msg/WeatherState.msg (breaking replacement), ios_backend, WeatherPanel.jsx,
  atmosphere_node (renamed to weather_solver), cigi_bridge, xplanecigi plugin

- DECIDED: **New sub-messages**: WeatherCloudLayer.msg, WeatherWindLayer.msg, MicroburstHazard.msg
  in sim_msgs/msg/. Cloud layer: CIGI cloud_type enum (0-10), coverage_pct, base_elevation_m,
  thickness_m, transition_band_m, scud_enable, scud_frequency_pct. Wind layer: altitude_msl_m,
  wind_speed_ms, wind_direction_deg, vertical_wind_ms, gust_speed_ms, shear_direction_deg,
  shear_speed_ms, turbulence_severity (0-1).
- REASON: Sub-messages keep WeatherState readable and allow reuse in SimSnapshot JSON sections.

- DECIDED: **AtmosphereState.msg extended** with NED wind vector (wind_north_ms, wind_east_ms,
  wind_down_ms), visible_moisture bool, turbulence_intensity float (0-1). These fields make
  AtmosphereState the single source of atmospheric truth at the aircraft position.
  FlightModelState.wind_* fields become read-back only (what JSBSim reports after writeback).
- REASON: Dual atmospheric truth problem — atmosphere_node and JSBSim computed independent
  values. With writeback, AtmosphereState is truth, FlightModelState is verification.
- AFFECTS: sim_msgs/msg/AtmosphereState.msg, all AtmosphereState consumers (air_data, engines,
  ice_protection, audio, ios_backend)

- DECIDED: **atmosphere_node renamed to weather_solver_node**, moves from src/core/atmosphere_node/
  to src/world/weather_solver/. New responsibilities: interpolate authored WeatherState at aircraft
  altitude, run external Dryden turbulence filter, sample Oseguera-Bowles microburst field,
  sum all wind contributions into single NED vector, publish AtmosphereState at 50 Hz.
  Standalone library + ROS2 node wrapper per framework convention.
- REASON: atmosphere_node's ISA-from-altitude math is a subset of what's needed. The new node
  owns interpolation + hazard composition + turbulence generation. Different job, different name.
- AFFECTS: src/core/atmosphere_node/ (delete), src/world/weather_solver/ (create),
  launch/sim_full.launch.py, CLAUDE.md node table

- DECIDED: **JSBSimAtmosphereWriteback** — new writeback module parallel to JSBSimElectricalWriteback
  and JSBSimFuelWriteback. Subscribes to /world/atmosphere, writes wind NED (fps), temperature
  deviation (Rankine), pressure (psf) into JSBSim property tree each step. JSBSim's internal
  atmosphere model is overridden every step.
- REASON: Closes the "weather injection gap" — the #1 known architectural gap. Without this,
  JSBSim always flies ISA regardless of what the instructor sets.
- AFFECTS: src/core/flight_model_adapter/ (new files), JSBSim property tree writes

- DECIDED: **External Dryden turbulence** computed in weather_solver, not JSBSim-native.
  Configurable model: 0=None, 1=MIL-F-8785C, 2=MIL-HDBK-1797A, 3=ESDU-85020.
  Selected per aircraft in weather config YAML. Deterministic via RNG seed field in WeatherState.
  Perturbation wind vector summed into AtmosphereState before writeback.
- REASON: Aircraft-independent, QTG-reproducible. JSBSim-native turbulence doesn't transfer to
  Helisim or C208 external FDM. CS-FSTD(A) doesn't mandate a specific model for FTD Level 2
  or FNPT II, but Leonardo SF260TW PRS-030390 requires Dryden per MIL-F-8785C. Selector
  covers all customer contracts.
- AFFECTS: src/world/weather_solver/ (Dryden solver library), aircraft config YAMLs

- DECIDED: **Microburst FDM-only, no visual phase 1**. Oseguera-Bowles 1988 analytical model in
  weather_solver standalone library. MicroburstHazard.msg carries field parameters (core_radius,
  shaft_altitude, intensity, lifecycle_phase). Sampled at aircraft position each step, added to
  wind sum. Visual via CIGI entity deferred to future phase.
- REASON: CS-FSTD does not require microburst for FTD/FNPT. X-Plane has no microburst visual
  primitive. FDM wind effect is the training point; visual is immersion.
- AFFECTS: src/world/weather_solver/ (microburst solver library), sim_msgs/msg/MicroburstHazard.msg

- DECIDED: **CIGI 3.3 standard packets for weather, user-defined for extensions**.
  Standard: 0x0A Atmosphere Control (global scalars), 0x0C Weather Control (per cloud/wind layer),
  0x0D/0x0E Maritime/Terrestrial Surface. User-defined: 0xC9 Dryden params, 0xCA Microburst field,
  0xCB FSTD control (variability, change_mode, deterministic_seed).
  cigi_bridge composes separate cloud/wind arrays from WeatherState into combined CIGI 0x0C packets.
  IOS features scoped to what CIGI can express — X-Plane compatibility locked.
- REASON: CIGI portability to non-XP IGs. Single transport layer. IOS doesn't expose anything the
  CIGI wire format can't carry. User-defined packets carry framework extensions that standard
  CIGI cannot express (turbulence model params, microburst field, FSTD determinism).
- AFFECTS: src/core/cigi_bridge/ (weather encoder), xplanecigi plugin (weather decoder)

- DECIDED: **xplanecigi plugin targets XP 12.4+**. Primary write path: XPLMSetWeatherAtLocation SDK
  (12.3+). Fallback: sim/weather/region/* datarefs for fields SDK doesn't cover (runway_friction,
  variability_pct, change_mode). Weather updates debounced via dirty flag in XPLM flight loop
  callback (~1 Hz). On reposition: update_immediately=1, regen_weather command, then clear.
  Cloud type remap: CIGI enum (0-10) → XP enum (0-3) via lookup table.
- REASON: SDK is Laminar's direction, supports future multi-point weather (DEP/DEST). Region
  datarefs as fallback for stability. XP weather updates are not per-frame (XP docs explicitly
  say "not intended to be used per-frame") — event-driven architecture matches our authored
  weather model.
- AFFECTS: ~/x-plane_plugins/xplanecigi/ (new weather_decoder module)

- DECIDED: **FSTD determinism controls** in WeatherState: variability_pct (0-1, forced 0 for QTG),
  evolution_mode (0-7, forced Static for QTG), deterministic_seed (uint32 for Dryden RNG).
  Maps to XP datarefs variability_pct and change_mode, and to CIGI user-defined packet 0xCB.
- REASON: QTG requires reproducible conditions. X-Plane's weather engine adds randomness via
  variability_pct and change_mode — must be zeroed/static for test runs.

- DECIDED: **CS-FSTD(A) turbulence requirements confirmed** — no specific turbulence model or test
  mandated for FTD Level 2 or FNPT II. Table 2 says "Control of atmospheric conditions" only.
  Wind shear tests are FFS-only (not FTD/FNPT). Microburst not required at any level below FFS.
  Customer contracts (e.g., Leonardo PRS-030390 citing MIL-F-8785C) are stricter than regulation.
- REASON: Confirms turbulence model selector approach — framework provides options, aircraft
  config specifies which model, customer contract determines minimum.
- AFFECTS: documentation only (no code impact, confirms design)

## 2026-04-17 — Claude Code (Weather Implementation Steps 1-8)

- DECIDED: **WeatherState v2 messages implemented** — WeatherCloudLayer, WeatherWindLayer,
  MicroburstHazard sub-messages created. WeatherState.msg replaced with layered structure.
  AtmosphereState.msg extended with wind NED + visible_moisture + turbulence_intensity.
- AFFECTS: sim_msgs (3 new .msg, 2 modified), all WeatherState/AtmosphereState consumers

- DECIDED: **atmosphere_node replaced by weather_solver_node** — new package at
  src/world/weather_solver/. Standalone libraries (no rclcpp): WeatherSolver (ISA +
  altitude-interpolated wind layers + angular interpolation), DrydenTurbulence (MIL-F-8785C
  forming filters, deterministic seed, altitude-scaled), MicroburstModel (Oseguera-Bowles 1988).
  ROS2 node wrapper publishes /world/atmosphere at 50 Hz.
- REASON: atmosphere_node was surface-wind-only with no interpolation or turbulence.
  weather_solver implements full altitude interpolation, Dryden turbulence generation,
  and microburst wind field sampling.
- AFFECTS: src/world/weather_solver/ (new), src/core/atmosphere_node/ (superseded),
  launch/sim_full.launch.py (swapped), 19 GoogleTest unit tests

- DECIDED: **JSBSimAtmosphereWriteback implemented** — writes wind NED (fps), temperature
  deviation delta-T (Rankine), and pressure (psf) into JSBSim property tree each step
  before Run(). Closes the weather injection gap. Pattern follows JSBSimElectricalWriteback.
- AFFECTS: src/core/flight_model_adapter/ (new writeback module + node integration)

- DECIDED: **CIGI weather encoder implemented** — cigi_bridge appends Atmosphere Control
  (0x0A, 32B) + Weather Control (0x0C, 56B) packets to UDP datagram. Cloud layers (id 1-3),
  precipitation (id 4-5), wind-only layers (id 10+). Dirty-flag gated (not every frame).
- AFFECTS: src/core/cigi_bridge/ (weather_encoder.hpp/cpp + node integration)

- DECIDED: **xplanecigi weather decoder implemented** — parses 0x0A/0x0C packets from CIGI
  datagram. PendingWeather struct stores decoded state. 1Hz XPLMFlightLoop writes to
  sim/weather/region/* datarefs (cloud base/top/coverage/type, wind layers, visibility,
  temperature, pressure, rain). Cloud type remap CIGI→XP via lookup table.
- AFFECTS: x-plane_plugins/xplanecigi/XPluginMain.cpp

- DECIDED: **air_data_node migrated from WeatherState to AtmosphereState** for turbulence
  and moisture fields. WeatherState subscription removed entirely from air_data_node.
- REASON: turbulence_intensity and visible_moisture moved to AtmosphereState in v2 design.
- AFFECTS: src/systems/air_data/src/air_data_node.cpp

- DECIDED: **IOS WeatherPanel v2 operational** — set_weather WS message (not sendCommand),
  unit conversion (°C→K, hPa→Pa, kt→m/s). Turbulence severity slider (0-100% UI → 0-1 wire).
  Microburst placement by bearing/distance from aircraft. activate/clear/clear_all WS handlers.
  Backend computes microburst lat/lon, caches and republishes with full WeatherState.
- AFFECTS: ios/frontend WeatherPanel, ios_backend weather + microburst handlers

## 2026-04-18 — Claude Chat / Claude Code (Runway Friction + Weather Bug Fixes)

- DECIDED: **Two-factor runway friction model implemented** — effective friction =
  surface_type_factor (from IG terrain) × contamination_factor (from instructor).
  Both factor tables in per-aircraft YAML (config.yaml → ground_friction section).
  JSBSim global properties ground/static-friction-factor and ground/rolling-friction-factor
  written each frame via JSBSimSurfaceWriteback. No JSBSim source patch required.
- REASON: JSBSim 1.2.1 FGGroundCallback has no friction virtuals — friction comes from
  FGGroundReactions (inherits FGSurface) via global properties. Two-factor model covers
  both terrain type (asphalt/grass/dirt) and contamination (dry/wet/icy) independently.
  YAML config enables per-aircraft tuning without recompilation. CS-FSTD(A) requires
  dry/wet/icy braking tests for FFS; FTD Level 2 requires statement of compliance.
- AFFECTS: flight_model_adapter (JSBSimSurfaceWriteback), IFlightModelAdapter interface,
  c172/ec135 config.yaml (ground_friction section)

- DECIDED: **Surface type from IG via CIGI HAT response** — xplanecigi plugin reads
  sim/flightmodel/ground/surface_texture_type dataref, maps XP enum (0-12) to framework
  enum (0-10), includes as material_code in HAT/HOT response bytes 24-35 (custom extension
  of standard 16-byte packet). cigi_bridge parses extended fields when pkt_size >= 36.
  Framework surface enum: UNKNOWN(0), ASPHALT(1), CONCRETE(2), GRASS(3), DIRT(4),
  GRAVEL(5), WATER(6), SNOW_COVERED(7), ICE_SURFACE(8), SAND(9), MARSH(10).
- REASON: IG-portable — any CIGI IG can provide surface type via HAT response material code.
  Enables bush flying (grass strips), terrain-aware ground handling. XP surface types
  discovered via DataRefTool: surf_none(0), surf_water(1), surf_concrete(2), surf_asphalt(3),
  surf_grass(4), surf_dirt(5), surf_gravel(6), surf_lake(7), surf_snow(8), surf_shoulder(9),
  surf_blastpad(10), surf_grnd(11), surf_object(12).
- AFFECTS: sim_msgs/msg/HatHotResponse.msg (surface_type + friction factor fields),
  xplanecigi plugin, cigi_bridge HAT response parser

- DECIDED: **IOS runway condition buttons** — category (DRY/WET/WATER/SNOW/ICE/SN+IC) ×
  severity (LIGHT/MEDIUM/MAX) = 16 states mapping 1:1 to X-Plane's runway_friction 0-15 enum.
  Immediate effect (no ACCEPT needed). set_runway_condition WS handler in ios_backend.
- AFFECTS: WeatherPanel.jsx, ios_backend_node.py

- DECIDED: **X-Plane runway_friction dataref is float** (not int). Plugin must use
  XPLMSetDataf() not XPLMSetDatai(). XPLMSetDatai() silently fails on float datarefs.
- AFFECTS: xplanecigi XPluginMain.cpp

- DECIDED: **X-Plane surface_texture_type dataref** — correct path is
  sim/flightmodel/ground/surface_texture_type (not surface_type). Discovered via DataRefTool.
- AFFECTS: xplanecigi XPluginMain.cpp dataref lookup

- DECIDED: **Cloud layer management uses dedicated add/remove WS messages** — not part of
  ACCEPT flow. add_cloud_layer appends to backend cache, remove_cloud_layer/clear_cloud_layers
  removes. ACCEPT only handles atmosphere fields (vis/qnh/oat/wind/turb). Prevents partial
  update wipe of cloud layers when accepting atmosphere changes.
- REASON: Backend's _last_weather_data.update() does shallow dict merge — replacing entire
  cloud_layers key on every ACCEPT wiped previously set clouds. Dedicated messages follow
  same pattern as microburst activate/clear.
- AFFECTS: WeatherPanel.jsx, ios_backend_node.py

- DECIDED: **CIGI encoder always emits 3 cloud packets** (layer_id 1, 2, 3) regardless of
  how many cloud_layers exist. Missing layers emit weather_enable=0. xplanecigi clears
  slots when weather_enable=0. Prevents stale cloud layers in X-Plane after removal.
- AFFECTS: weather_encoder.cpp, xplanecigi XPluginMain.cpp

- DECIDED: **regen_weather command for instant cloud updates** — xplanecigi fires
  sim/operation/regen_weather XPLMCommand after cloud changes. Causes brief visual pop but
  clouds appear in ~5 seconds instead of ~60. Configurable via plugin config file:
  regen_weather=always|ground_only|never. Default: ground_only (fires when on ground or
  first cloud update; lets 60s fade work in flight for realism).
- AFFECTS: xplanecigi XPluginMain.cpp, plugin config file

- DECIDED: **Weather state feedback loop fixed** — backend broadcasts weather_state WS
  message after every publish_weather() and stores in _latest for new connections. Frontend
  displays active cloud layers from this broadcast. Root cause of initial failure: stale
  ament_python build artifacts (colcon build without --symlink-install left old .py in
  install/). Resolution: rebuild with --symlink-install, document in CLAUDE.md as
  ios_backend build constraint.
- AFFECTS: ios_backend_node.py, useSimStore.js, WeatherPanel.jsx

- DECIDED: **ament_python --symlink-install required for ios_backend** — colcon build
  copies Python files to install/. Without --symlink-install, source edits are invisible
  to the running backend. Every ios_backend change requires:
  colcon build --packages-select ios_backend --symlink-install
  Added to CLAUDE.md workflow constraints.
- AFFECTS: build workflow documentation

- DECIDED: **JSBSim friction limitation: global only** — JSBSim 1.2.1 applies a single
  global surface friction factor to all gear legs. No per-gear differentiation without
  patching JSBSim source (FGGroundCallback has no friction virtuals). If one gear is on
  asphalt and another on grass, both see the same factor. Acceptable: C172 wheelbase ~2m,
  both mains always on same surface. Documented limitation.
- AFFECTS: documentation only (architectural constraint)

## 2026-04-18 — Claude Chat (Weather Step 10 Architecture Lock — PARKED for future implementation)

- DECIDED: **Localized weather via CIGI Environmental Region Control + Weather Control Scope=Regional**. Each authored patch emits one CIGI Environmental Region Control packet (0x0B, ID=11, 48 bytes) defining a circular region (SizeX=SizeY=0, CornerRadius=radius_m) + N Weather Control packets (0x0C) with Scope=Regional referencing that Region ID — one per cloud layer, wind layer, precipitation override, visibility override, or temperature override in the patch.
- REASON: CIGI 3.3 ICD section 4.1.11 documents circular environmental regions as the intended pattern for approximating weather cells. Fully standard CIGI, no user-defined packets required for localized weather. Portable to non-XP IGs. Entity scope (Scope=2) was considered and rejected — Entity scope attaches weather to moving models, not stationary geographic regions.
- AFFECTS: sim_msgs/WeatherPatch.msg (new), sim_msgs/WeatherState.msg (add patches[]), src/core/cigi_bridge/ (weather_sync module), x-plane_plugins/xplanecigi/ (region tracking), ios_backend (patch lifecycle), ios/frontend WeatherPanel (tabs)

- DECIDED: **WeatherPatch.msg sub-message**:
    uint16  patch_id                 # maps to CIGI Region ID, monotonic per session
    string  role                     # "departure" | "destination" | "custom"
    string  label                    # IOS tab title
    float64 lat_deg
    float64 lon_deg
    float32 radius_m                 # CIGI CornerRadius
    # Overrides — empty/NaN = no override for that field
    sim_msgs/WeatherCloudLayer[] cloud_layers
    sim_msgs/WeatherWindLayer[]  wind_layers
    float32 visibility_m             # -1 = no override
    float32 temperature_k            # NaN = no override
    float32 precipitation_rate       # -1 = no override
    uint8   precipitation_type       # 0 = no override
  WeatherState.msg gains `WeatherPatch[] patches`. No transition_perimeter_m, no max_altitude_msl_ft — both auto-derived at encode time.
- REASON: One unified patch concept scales from "extra wind at destination" to "CB in valley" without type-specific messages. Each patch = one CIGI Region + N layers.
- AFFECTS: sim_msgs/msg/WeatherPatch.msg (new file), sim_msgs/msg/WeatherState.msg (1 line addition)

- DECIDED: **Patch radius defaults and bounds**. Custom patches (CB cells, fog pockets, map-placed) default to **3 nm** (~5.5 km) — matches typical single-cell thunderstorm (5–15 km diameter). Departure/Destination patches default to **10 nm** (~18.5 km) — covers terminal area / approach corridor. Slider bounds: **1 nm – 50 nm**. Below 1 nm XP's internal blend smears the patch into nothing; above 50 nm the instructor should be editing Global instead. XP default (XPLM_DEFAULT_WXR_RADIUS_NM=30 nm) rejected as too large for FTD scenarios — student would traverse full patch in <15 min of descent.
- REASON: Realistic weather cell sizes aligned with meteorological norms, not with XP's generic authoring default.
- AFFECTS: ios/frontend WeatherPanel (slider defaults per patch role), sim_msgs/WeatherPatch.msg (documentation comment)

- DECIDED: **cigi_bridge owns sent-state tracking**. New std::map<uint16_t, SentPatch> sent_patches_ diffs against incoming /world/weather each publish. Added → Region Control (State=Active) + Weather Control(s) (Weather Enable=1). Removed → Region Control (State=Destroyed). Content changed → re-emit (CIGI idempotent for same Region ID). Layer removed from patch → Weather Control with Weather Enable=0. Periodic full re-assertion every 10 s (UDP loss compensation). Destroy packet retry 3× over 1 s. On reposition rising edge: flush sent_patches_ (IG Mode=Reset already clears IG-side state). Content hash buckets float fields (lat/lon to 6 decimals, radius to 10 m) before comparison to avoid thrash from UI drag events.
- REASON: CIGI is declarative — packets set state that persists in the IG until explicitly changed or destroyed. UDP loss means any single packet can be dropped. Upstream nodes publish snapshot desired state only; cigi_bridge translates to persistent wire semantics.
- AFFECTS: src/core/cigi_bridge/ (new weather_sync.hpp/cpp, state tracking, periodic re-assertion timer)

- DECIDED: **xplanecigi plugin mirrors region tracking**. std::map<uint16_t, LiveXPPatch> xp_known_regions_. State=Active new → queue XPLMSetWeatherAtLocation for next flight loop. State=Active existing with position or radius changed → XPLMEraseWeatherAtLocation(old_lat, old_lon) + XPLMSetWeatherAtLocation(new) (XP cannot move a sample in place). State=Destroyed → XPLMEraseWeatherAtLocation + drop from map. Weather Control packets accumulated into LiveXPPatch's XPLMWeatherInfo_t until flight loop dispatches. radius_nm = corner_radius_m × 0.000539957. max_altitude_msl_ft = max(cloud_layer.alt_top_m × 3.28084, wind_layer.altitude_msl_m × 3.28084) + 2000 ft, fallback to XPLM_DEFAULT_WXR_LIMIT_MSL_FT (10000 ft) if no layers. Transition perimeter forwarded if present, else derived as 10% of radius. On plugin load: no known state — rely on next 10 s Host re-assertion to repopulate.
- REASON: XPLMWeatherInfo_t carries radius_nm and max_altitude_msl_ft explicitly (confirmed via XP SDK docs and XPLM_DEFAULT_WXR_* constants). Plugin is last line of defense against zombie weather — must track and erase explicitly, cannot rely on CIGI alone.
- AFFECTS: x-plane_plugins/xplanecigi/XPluginMain.cpp (weather_sync extension, region map, flight loop dispatch)

- DECIDED: **Vertical extent and transition perimeter auto-derived**, not instructor-exposed. Vertical extent = max authored layer altitude + 2000 ft buffer. Transition perimeter = 10% of radius. Neither appears as a UI control.
- REASON: Surfacing these as separate knobs creates inconsistency traps (instructor sets clouds to 35k ft but forgets vertical limit at 10k ft → clouds don't render above 10k). Vertical extent IS the weather authored in the column view. Transition is a CIGI nicety, not a training concern.
- AFFECTS: cigi_bridge (derive at encode time), xplanecigi (derive at XP write time), ios/frontend (no UI element)

- DECIDED: **Region ID allocation** — ios_backend owns monotonic uint16 counter. Reset on session-start command or IOS process restart. Allocated when instructor creates a patch, stamped into WeatherPatch.patch_id on the wire. No reuse within session. Reposition preserves IDs (patches survive unless explicitly cleared). Session reset + IG Mode=Reset guarantees no collision.
- REASON: Deterministic ID lifetime simplifies debugging. uint16 (65535 values) is inexhaustible for realistic session lengths. No reuse eliminates "is this Region ID 5 the same cell from 10 minutes ago?" ambiguity.
- AFFECTS: ios_backend (patch_id counter, session-start hook, patch create/destroy handlers)

- DECIDED: **IOS UI — vertical MSL column with tabbed contexts**. Pattern inspired by X-Plane 12 manual weather UI, adapted for FTD. Center: vertical altitude column (0 ft MSL → 50,000 ft MSL) with draggable cloud layer bands and wind chips at altitudes. Left rail: selected-layer properties. Right rail: atmospheric conditions (visibility, temperature, altimeter, precipitation, runway conditions) + for patch tabs only, a radius slider (1–50 nm). Top tabs: Global / Departure / Destination / [custom patches]. Departure/Destination tabs auto-create on airport selection in Position panel, auto-destroy on change. Each tab is an independent full weather authoring (not inherited from Global). "Copy from Global" button on each patch tab pre-fills all fields from Global state. Dropped from X-Plane inspiration: Manual/Real-world toggle (FTD is always authored), Variation Across Region / Evolution Over Time (hardcoded static in plugin), Thermals (not relevant for commercial training).
- REASON: Vertical column matches pilot mental model (stacked layers). Tabs scale from basic Global-only to complex multi-patch without a second interaction model. Independent tabs map 1:1 to CIGI wire (one tab = one Region + N Weather Controls). Copy-from-Global gives inheritance ergonomics without inheritance complexity.
- AFFECTS: ios/frontend WeatherPanel.jsx (major rework), new components WeatherLayerColumn / AtmosphericConditionsPanel / PatchTabs / RadiusSlider

- DECIDED: **Map-based patch authoring deferred to phase 2**. Phase 1 (tabs) handles Dep/Dest + small number of custom patches — covers most training workflows. Phase 2 extends the shared Position+Weather Leaflet map with weather overlay layer, drag-to-move patch markers, drag-to-resize radius circles. Both phases edit the same WeatherState.patches[] via the same React components.
- REASON: Tab-based authoring is faster to implement, sufficient for authored terminal weather, and proves out the data model before committing to shared-map architecture.
- AFFECTS: (phase 2 scope only — not in this decision)

- DROPPED: **XPLMSetWeatherAtAirport**. Superseded by Environmental Region Control centered on airport ARP with Weather Control Scope=Regional. Non-portable XP-specific API. ICAO-keyed only (no arbitrary lat/lon). Strictly weaker than regional control. Originally considered in the 2026-04-17 xplanecigi plugin decision — withdrawn.
- DROPPED: **User-defined CIGI packets 0xC9 (Dryden params), 0xCA (microburst field), 0xCB (FSTD determinism)**. Microburst and Dryden turbulence are FDM-only — not visible in the IG per 2026-04-17 microburst decision. FSTD determinism (variability_pct=0, change_mode=Static) hardcoded in xplanecigi plugin — no need to transmit over the wire. Eliminates three framework-specific extension packets from the wire protocol.
- DROPPED: **CIGI Weather Entity (Scope=Entity) for CB cells**. Entity scope attaches weather to moving models. Regional scope + Environmental Region Control is the correct CIGI mechanism for stationary localized weather. Confirmed via ICD section 4.1.11.

- DEFERRED: **Test plugin to verify radius_nm and max_altitude_msl_ft field presence in XPLMWeatherInfo_t** across XP 12.4+ point releases. Official developer.x-plane.com struct documentation is inconsistent with XPPython3 docs on field visibility; constants (XPLM_DEFAULT_WXR_RADIUS_NM, XPLM_DEFAULT_WXR_LIMIT_MSL_FT) confirm behavior exists, but a 20-line test plugin should verify exact binding before implementation begins.
- AFFECTS: ~/x-plane_plugins/xplanecigi_wxr_test/ (new scratch plugin, to be built when Step 10 un-parks)

## 2026-04-18 — Claude Chat (Weather Step 10 — WeatherPatch.msg field evolution)

Corrects field-naming decisions from the preceding "Weather Step 10 Architecture Lock" entry. The architecture itself is unchanged; only message field names and override-sentinel conventions were refined during task card drafting. Reflects the implementation-ready shape of WeatherPatch.msg that Slice 1 will create.

- DECIDED: **role → patch_type**. Field renamed from `role` to `patch_type`. "Role" was judged misleading — a role implies behavioral responsibility, whereas this field classifies the patch by anchoring kind.
- REASON: Reads as "what type of patch is this" at consumer sites. Leaves room for future `patch_type="route"` (route-anchored patches) without semantic conflict with a behavioral-role notion.
- AFFECTS: sim_msgs/WeatherPatch.msg, cigi_bridge weather_sync, ios_backend patch handlers, ios/frontend WeatherPanelV2

- DECIDED: **Departure/destination collapsed into single "airport" patch_type**. Values reduced from `"departure" | "destination" | "custom"` to `"airport" | "custom"`. Whether a given airport patch is at the departure or destination airport is context (which ICAO is stamped on it), not a separate type.
- REASON: Dep and Dest aren't semantically different patch kinds; both are "weather anchored to an airport." Reduces enum cardinality and eliminates the edge case where Dep and Dest are the same airport.
- AFFECTS: sim_msgs/WeatherPatch.msg, ios/frontend patch tab ordering logic

- DECIDED: **icao field added to WeatherPatch**. New `string icao` field. Empty string for `patch_type="custom"`, ICAO code (e.g., "EBBR") for `patch_type="airport"`.
- REASON: Enables ios_backend to re-resolve lat/lon if airport DB updates, enables IOS tab display without reverse-lookup, supports future SimSnapshot save/load with stable airport references. Original architecture entry didn't surface the ICAO retention need.
- AFFECTS: sim_msgs/WeatherPatch.msg, ios_backend airport search handler

- DECIDED: **Explicit override flags for scalar fields, empty arrays for layer overrides**. Replaces the sentinel-based override scheme (-1 / NaN / 0) from the architecture lock entry with explicit booleans:

      bool    override_visibility
      float32 visibility_m
      bool    override_temperature
      float32 temperature_k
      bool    override_precipitation
      float32 precipitation_rate
      uint8   precipitation_type

  Layer overrides remain array-based: empty `cloud_layers[]` = no cloud override, empty `wind_layers[]` = no wind override. `precipitation_rate` and `precipitation_type` are gated by a single `override_precipitation` flag (rate without type, or type without rate, is non-sensical).
- REASON: Sentinel values were inconsistent across fields (-1 for visibility, NaN for temperature, 0 for precipitation_type) and created implicit "is this a real zero or a sentinel?" ambiguity. Explicit flags cost 3 bytes on the wire but eliminate override ambiguity and map 1:1 to IOS frontend "use global / override" toggles.
- AFFECTS: sim_msgs/WeatherPatch.msg, cigi_bridge weather encoder (check flag before emitting Weather Control packet for scalar field), ios/frontend override toggles

- UNCHANGED from the architecture lock entry: patch_id (uint16, monotonic per session), label (string, IOS tab title), lat_deg / lon_deg (float64), radius_m (float32), cloud_layers[] / wind_layers[] sub-messages. Radius defaults (3 nm custom, 10 nm airport), bounds (1–50 nm), CIGI Environmental Region Control + Weather Control Scope=Regional wire mapping, cigi_bridge sent-state tracking discipline, xplanecigi plugin region tracking, IOS UI vertical-MSL-column pattern, and phase 2 map deferral — all as previously decided.

## 2026-04-18 — Claude Chat (Weather Step 10 — WeatherPatch.msg ground_elevation_m addition)

Corrects the WeatherPatch.msg shape established in the 2026-04-18 "WeatherPatch.msg field evolution" amendment. A single field is added; no other fields change.

- DECIDED: **ground_elevation_m added to WeatherPatch**. New `float64 ground_elevation_m` field between `radius_m` and the cloud_layers[] block.
- REASON: IOS frontend needs a per-patch ground reference to display live AGL alongside the authored MSL values on cloud and wind altitude sliders — matches the X-Plane 12 manual weather UI pattern. Without this, AGL annotation either falls back to the global station elevation (wrong for patches far from station) or forces the instructor to mentally convert.
- POPULATED BY: ios_backend on patch create or lat-lon change:
  - `patch_type="airport"` — ARP elevation from airport DB (navaid_sim `SearchAirports` service) via ICAO lookup
  - `patch_type="custom"`  — SRTM terrain elevation from `/navaid_sim/get_terrain_elevation` service at patch lat/lon
  Default: NaN until populated.
- CONSUMED BY: ios/frontend WeatherPanel (Slice 5) for live AGL/MSL display. NOT consumed by cigi_bridge or xplanecigi — framework-to-IG wire uses MSL only; plugin derives its own ground reference via `XPLMProbeTerrainXYZ` at the patch lat/lon (X-Plane must render against its own terrain, not SRTM, to stay consistent with its rendered world).
- ACCEPTED DRIFT: For custom patches in hilly terrain, IOS-displayed AGL and plugin-rendered ground reference may differ by tens of meters where SRTM and X-Plane terrain diverge. Acceptable for FTD training. Mitigation path if needed: restore navaid_sim's full-resolution SRTM data (replacing the current compressed 3GB dataset).
- AFFECTS: sim_msgs/WeatherPatch.msg (this slice), ios_backend patch handlers (Slice 4), ios/frontend WeatherPanel sliders (Slice 5)

- UNCHANGED: CIGI wire protocol (Environmental Region Control + Weather Control Scope=Regional) carries no ground elevation field — plugin probes terrain at the patch lat/lon. cigi_bridge weather encoder and WeatherSync diff logic ignore ground_elevation_m. xplanecigi plugin ignores the field even though it receives the full WeatherPatch via /world/weather routing (not via CIGI — ground_elevation_m is a IOS-display-only concept).

## 2026-04-18 — Claude Chat (Weather Step 10 — Slice 2c.2: re-assertion rip-out)

Reverts the UDP-loss compensation mechanisms added in Slice 2c. Event-driven WeatherSync only.

- DECIDED: **10-second periodic re-assertion REMOVED from WeatherSync.** On a dedicated Host↔IG LAN, UDP packet loss is negligible (<0.01% measured). Re-assertion's practical cost — zombie patches after publisher shutdown, sample stacking in X-Plane weather blend, visible cloud flicker per tick, two-way test assertion complexity — outweighed its theoretical reliability benefit.
- DECIDED: **Destroy-packet retry (3× over ~1s) REMOVED.** Same reasoning. One Destroy packet per removal. If a Destroy is ever dropped (statistically rare), the next /world/weather message that triggers a content diff will re-emit correctly, because sent_patches_ tracks state deterministically.
- KEPT: **startup_reset_pending_ flag on cigi_bridge activation.** Sends IG Mode=Reset once at startup to clear any stale X-Plane weather from previous sessions. Not re-assertion; it's initialization hygiene.
- KEPT: **flush_on_reposition() clearing sent_patches_.** Reposition emits IG Mode=Reset, plugin erases all applied patches. WeatherSync must clear sent_patches_ to match.
- KEPT: **Slice 2c.1 content-hash short-circuit.** Unchanged patches don't re-emit on repeated publishes.
- REASON: Observed in testing: publisher shutdown left zombie patches alive for up to 10s, and re-assertion ticks caused sample stacking that produced visible cloud density buildup. The failure modes introduced by re-assertion were worse than the failure mode it was meant to prevent. On LAN, single-send is reliable enough.
- AFFECTS: WeatherSync class (API narrower), cigi_host_node (weather_reassert_timer_ removed), test_weather_sync.cpp (7 tests removed, 1 test simplified)
- UNCHANGED: CIGI wire protocol, plugin code, WeatherState.msg, WeatherPatch.msg, any other package

## 2026-04-18 — Claude Chat (Weather Step 10 — Pipeline reference, Phase 1 complete)

This entry captures the end-state of the weather pipeline after Step 10 Phase 1 (Slices 1 through 3c + follow-up fixes 3b.1, 3b.2, and rip-out 2c.2). It supersedes the working-state descriptions in earlier Slice entries for architectural reference — individual Slice entries remain valid as change-log history.

### End-to-end flow

**IOS / ios_backend** publishes `sim_msgs/WeatherState` on `/world/weather`. Message carries both global weather (temperature_sl_k, pressure_sl_pa, visibility_m, wind_layers, cloud_layers, precipitation fields, station_icao, station_elevation_m) AND a `patches[]` array of `WeatherPatch` entries for localized weather. Patches are defined in Slice 1 (`WeatherPatch.msg`), extended in Slice 1.1 to add `ground_elevation_m` (IOS-display-only, not on-wire).

**cigi_bridge** (simulator_framework/src/core/cigi_bridge) subscribes to `/world/weather` and emits CIGI 3.3 UDP datagrams to the Image Generator. Two separate wire paths:

1. **Global weather** — encoded into one Atmosphere Control packet (0x0A, 32B) plus N Weather Control packets (0x0C, 56B) with Scope=Global. Layer IDs 1-3 for cloud layers, 4-5 for precipitation, 10+ for wind layers. This path has existed since pre-Step 10.

2. **Regional patches** — encoded via `WeatherSync` class. Each patch gets one Environmental Region Control packet (0x0B, 48B) plus N Weather Control packets (0x0C) with Scope=Regional referencing the patch's `patch_id` as the CIGI Region ID. Framework layer-ID allocation within a patch: cloud layers 1-3, wind layers 10-12, scalar overrides 20 (vis+temp) and 21 (precipitation).

Both paths share one UDP datagram per frame alongside IG Control + Entity Control. Scratch buffer sized at 4096 bytes (worst case under framework limits is ~3,640 bytes).

**WeatherSync** is event-driven (post-2c.2 simplification): diffs `weather.patches` against its `sent_patches_` map on every `/world/weather` message. Emits Region(Active)+layers for new or changed patches, Region(Destroyed) for removed patches. Slice 2c.1's content-hash short-circuit suppresses re-emit for identical-content repeats (prevents X-Plane sample stacking on repeated publishes). `flush_on_reposition()` clears `sent_patches_` so post-reposition state is fresh. No periodic re-assertion, no destroy retry — UDP-loss compensation was removed in 2c.2 after it caused more issues than it prevented.

Framework limits: max 10 patches per WeatherState (enforced with truncation warning), max 3 cloud layers per patch, max 3 wind layers per patch. IOS enforces the realistic ceiling of 5 patches.

**xplanecigi plugin** (separate repo at ~/x-plane_plugins/xplanecigi) receives CIGI datagrams, parses by packet ID, dispatches to state accumulators:

- 0x01 IG Control → `g_ig_mode` transitions. Reset→Operate triggers terrain probe stability check. X→Reset triggers `cleanup_regional_weather()` (erase all applied samples + clear tracking).
- 0x03 Entity Control → ownship position/attitude to X-Plane datarefs.
- 0x0A Atmosphere Control → global scalars into `pending_wx` with NaN/range clamps (Slice 3b.2 — temperature −90 to +60°C, pressure 800-1100 hPa, visibility > 100 m, wind speed 0-150 m/s; fallbacks are ISA defaults).
- 0x0B Environmental Region Control → `g_pending_patches` map entry with geometry (lat, lon, circular radius, rotation, transition perimeter).
- 0x0C Weather Control → scope-dispatched: Global writes to `pending_wx`; Regional routes layers into the matching `g_pending_patches[region_id]` entry (cloud/wind/scalar-override sub-arrays). Unknown scope logged and ignored.

A 1 Hz flight loop applies both paths:
- Global: writes `pending_wx` to `sim/weather/region/*` datarefs if `pending_wx.dirty`. Gated `regen_weather` command (ground_only by default) for cloud visual refresh.
- Regional: diffs `g_pending_patches` against `g_xp_applied`. New or changed → `XPLMSetWeatherAtLocation` with full `XPLMWeatherInfo_t` (Option B overlay: seeded from `pending_wx` with clamps, patch fields overlaid on top, wind layers default to XPLM_WIND_UNDEFINED_LAYER, ground_altitude_msl from `XPLMProbeTerrainXYZ` at patch lat/lon). Removed → `XPLMEraseWeatherAtLocation`. All Set/Erase calls wrapped in `XPLMBeginWeatherUpdate()` + `XPLMEndWeatherUpdate(1, 1)` (incremental + updateImmediately) so changes take effect immediately instead of morphing over minutes.

`XPluginDisable` calls `cleanup_regional_weather()` so weather doesn't persist into next session.

### Wire protocol constants

- Scope bit field (0x0C packet byte 7 bits 0-1): 0=Global, 1=Regional, 2=Entity (unused), 3=reserved.
- Region State (0x0B packet byte 4 bits 0-1): 0=Inactive, 1=Active, 2=Destroyed.
- `Merge Weather Properties` bit (0x0B byte 4 bit 2) always set — framework always merges.

### Unit conventions across the pipeline

- WeatherState.msg: temperature in Kelvin, pressure in Pa, visibility in meters, wind in m/s
- CIGI wire (0x0A): temperature in °C, pressure in hPa/mbar, visibility in meters, wind in m/s
- PendingWeather (plugin internal): temperature in °C, pressure in hPa, cloud_type XP enum 0-3, coverage 0-1
- PendingPatch (plugin internal, CIGI-raw): temperature in °C, cloud_type CIGI enum 0-15, coverage_pct 0-100
- XPLMWeatherInfo_t (XP SDK): temperature in °C, pressure_sl in Pa, coverage 0-1, cloud_type XP enum 0-3, radius_nm, altitudes in ft MSL

Unit boundary comments in build_weather_info document all three conventions side-by-side.

### IOS authoring model (for reference; not yet fully implemented)

Cloud bases authored in ft-AGL (IOS frontend convention), converted to m-MSL using patch `ground_elevation_m` (from airport DB for airport patches, SRTM service for custom patches). Wire format carries m-MSL. Plugin derives its own ground reference via `XPLMProbeTerrainXYZ` at patch lat/lon (may differ slightly from IOS's SRTM-based reference in hilly terrain — acceptable drift for FTD training).

### What is NOT in this pipeline (intentionally)

- No user-defined CIGI packets (0xC9 Dryden, 0xCA microburst, 0xCB FSTD) — all three rejected in earlier design rounds. Dryden and microburst are FDM-only (computed in weather_solver_node, written to JSBSim property tree, never reach the IG). FSTD control (variability, change_mode, determinism) is framework-enforced, not IG-driven.
- No X-Plane-specific `sim/weather/region/set_weather_at_airport` datarefs — we use the standard CIGI + XP SDK path only, so the architecture stays portable to non-XP IGs.
- No periodic weather re-assertion or UDP-loss retry (removed in 2c.2).

### Cross-repo commit state (as of this entry)

**simulator_framework** — commit 4cc15db (Slice 2c.2).
**xplanecigi plugin** — commit 44ced68 (Slice 3b.2). Plugin capabilities: 0x0B and Scope=Regional 0x0C decoders (Slice 3a, 1d4dba7), XPLMSetWeatherAtLocation wiring with Option B overlay (Slice 3b, 946e9ad), regional NaN clamps (Slice 3b.1, 216f040), global 0x0A NaN clamps (Slice 3b.2, 44ced68), Reset cleanup and XPluginDisable cleanup (Slice 3c, 0dfa847).

Note: the xplanecigi plugin currently lives under `x-plane_plugins/xplanecigi/` inside this same `simulator_framework` repo, so all five hashes above are reachable from `git log -- x-plane_plugins/xplanecigi/` in the same repo. Phrasing ("separate repo") preserved for the eventual split.

### What comes next

Slice 4: ios_backend patch lifecycle. WebSocket handlers for add/update/remove/clear_patches, patch_id allocator, ground_elevation_m populate from airport DB / SRTM service.

Slice 5: IOS frontend WeatherPanelV2 with live AGL/MSL display. X-Plane 12 pattern, draggable MSL layer columns with computed AGL alongside.

## 2026-04-20 — Claude Code (WeatherPatch visual rendering — XP SDK limitation, parked)

- FOUND: `XPLMSetWeatherAtLocation` regional weather samples do NOT produce visible weather effects (visibility, clouds) at realistic training radii (10–50 NM). Plugin correctly emits Region Control (0x0B) + Weather Control Scope=Regional (0x0C) packets; xplanecigi correctly decodes them and calls `XPLMSetWeatherAtLocation` with a fully populated `XPLMWeatherInfo_t`; the SDK call returns success but X-Plane's visual rendering shows no discernible localized change.
- HYPOTHESIS: X-Plane 12's weather engine blends samples over distances ≫ 10 NM, so a single sample inside a 10–50 NM circle gets smoothed into the global field. Also per SDK docs, the call is "not intended to be used per-frame; should be called only during the pre-flight loop callback" — we call it from a 1 Hz flight loop, which may violate intended timing. Third-party XP12 weather plugins (Active Sky, VisualXP) do not use this API for localized fog/vis either — they manipulate global datarefs directly.
- STATE: CIGI patch pipeline and plugin SDK-call code are all correct per specification. No framework bug. Authoring UI (Slice 5b-iii) works; wire format (Slice 5b-ii) works; plugin decode + SDK call (Slice 3a/3b) work. The **visual** output stage is the limiter.
- DECIDED: Accept as X-Plane SDK limitation. Patches remain functional for FDM once Slice 5b-iv-a implements `weather_solver` awareness of `msg.patches`. FDM-path overrides (temperature, wind) are the certification-critical ones anyway. Visual patches revisit only if:
    (a) a customer explicitly requires localized visual weather, OR
    (b) Laminar publishes an alternative SDK path, OR
    (c) we find a global-dataref-manipulation workaround (matching Active Sky / VisualXP's approach).
- AFFECTS: bugs.md (known-limitation entry added); no code changes. Slice 5b-iv-a (solver patch-awareness) is the remaining work to make patches useful for FDM / QTG.

## 2026-04-20 — Claude Chat (Weather Step 11 — XPLMSetWeatherAtLocation visual limitation)

Diagnostic finding recorded after end-to-end testing during Slice 5b rollout. Confirms that the CIGI regional weather pipeline and xplanecigi plugin SDK call are correct per documentation, but X-Plane 12's visual rendering does not produce a discernible localized weather effect at training-scale radii. Expands the earlier 2026-04-20 entry with a full pipeline trace, richer hypotheses, and explicit revisit conditions so a future investigator doesn't re-walk the diagnostic.

- FOUND: XPLMSetWeatherAtLocation produces no visible weather change at 10–50 NM radii in X-Plane 12. Tested with visibility contrast (global 40 km vs patch 300 m, 10 NM radius at EBBR) and cloud-layer contrast (global clear vs patch overcast cumulus, 10 NM radius). Aircraft positioned at patch center on runway; no visual difference observed compared to global-only weather.

- VERIFIED HEALTHY: end-to-end wire path:
  - IOS frontend authoring (Slice 5b-iii) — OVERRIDE pills functional
  - ios_backend WeatherState.patches[] emission — confirmed by `ros2 topic echo /world/weather` showing correct patch fields
  - cigi_bridge Region Control + Weather Control Scope=Regional packet emission — confirmed by plugin log:
        `xplanecigi: Region N state=1 lat=X lon=Y radius=Z`
        `xplanecigi: Region N scalar override layer 20 vis=W temp=T en=1`
  - xplanecigi plugin decode and XPLMWeatherInfo_t construction including scalar-override overlay (Option B overlay per Slice 3b)
  - XPLMSetWeatherAtLocation SDK call — confirmed by plugin log:
        `xplanecigi: applied region N at X,Y ground=Gm radius=Rnm`
  - XPLMBeginWeatherUpdate / XPLMEndWeatherUpdate(1, 1) wrapping — confirmed in code

- HYPOTHESES (not independently verified, documented for future investigators):
  - X-Plane 12's weather engine blends samples over distances significantly larger than 10–50 NM. A small regional sample may be smoothed out by surrounding global weather.
  - Per SDK docs: "This call is not intended to be used per-frame. It should be called only during the pre-flight loop callback." We call from a 1 Hz flight loop, which is not per-frame but also not pre-flight. May violate intended timing.
  - Community pattern: third-party XP12 weather plugins (Active Sky, VisualXP) do not use this API for localized fog/visibility. They manipulate global `sim/weather/region/*` datarefs dynamically. This is circumstantial evidence that XPLMSetWeatherAtLocation is not practical for the use case.
  - Combining per-region SDK calls with global dataref writes in the same plugin may create a state-machine conflict in X-Plane's internal weather blending.

- DECISION: Accept as X-Plane SDK limitation. FDM patch path (Slice 5b-iv-a) delivers the training-critical behavior (OAT shift, wind shift) that certification and QTG reproducibility require. Visual localized weather is cosmetic for instructor immersion — important but not blocking.

- REVISIT CONDITIONS:
  - Customer explicitly requires localized visual weather in a specific deliverable
  - Laminar publishes alternative SDK path or XP 12 visual blending changes
  - We build a standalone test plugin (outside simulator_framework) that exercises XPLMSetWeatherAtLocation in isolation, to disambiguate SDK limitation from combine-with-datarefs conflict
  - We implement a workaround path: on aircraft entering a patch radius, manipulate global datarefs dynamically. This is an architectural alternative to regional samples but would require significant plugin refactoring.

- AFFECTS: No code changes. Documentation only. CIGI patch pipeline and xplanecigi plugin remain as built — correct per spec, waiting on X-Plane to render what we send.

## 2026-04-21 — Claude Code

Slice 5b-iv — per-patch weather overrides complete coverage.

- DECIDED: Extended `WeatherPatch.msg` with 6 fields — `override_humidity`+`humidity_pct`, `override_pressure`+`pressure_sl_pa`, `override_runway`+`runway_friction`. Completes per-patch override coverage; previously these were authored in the V2 UI (since 5b-iii) but dropped at the wire.
- REASON: Close the last 5b-era wire gap so every global WeatherState field has a per-patch override. Enables field-QNH training (patch pressure = altimeter shift, NOT density shift) and localized runway surface conditions (EBBR dry / EBAW snow). Humidity is wire-only, reserved for future cockpit hygrometer and CIGI regional moisture.
- AFFECTS: `sim_msgs/msg/WeatherPatch.msg`, `weather_solver/` (applies pressure override; emits effective_runway_friction), `ios_backend_node.py` (patch field plumbing), `ios/frontend/src/components/panels/weatherV2/weatherUnits.js` (wire serialization).

- DECIDED: Route patch-aware runway friction through `AtmosphereState.effective_runway_friction` (new field) — Approach B over Approach A (which would duplicate haversine + find_active_patch in flight_model_adapter).
- REASON: Centralizes all patch-resolution logic in weather_solver. Flight_model_adapter reads the effective value from `/world/atmosphere` without needing patch awareness. Pattern reusable for future scalars that need patch overrides and have non-weather_solver consumers.
- AFFECTS: `sim_msgs/msg/AtmosphereState.msg` (new `uint8 effective_runway_friction`), `weather_solver::AtmoResult` + `compute()`, `flight_model_adapter_node.cpp` (drops `/world/weather` subscription; reads runway from atmosphere instead). "On-ground" gate stays in `JSBSimSurfaceWriteback` via FDM state.

- DECIDED: Pressure override is altimeter-only (shifts `qnh_pa` output); does NOT propagate to `P_isa` or density.
- REASON: Matches FTD training reality — field QNH vs. standard 1013 is an altimeter-setting exercise, not a density-physics exercise. Current weather_solver behavior preserved for global pressure; patch override just substitutes the `qnh_pa` value when inside the radius.
- AFFECTS: `weather_solver.cpp::compute()` pressure override block.

- DECIDED: atmosphere_node is dead code (not launched; only weather_solver_node is in sim_full.launch.py). Kept in tree, orphaned. Not removed in this slice.
- REASON: Verified during Slice 5b-iv audit. Both atmosphere_node and weather_solver_node have AtmosphereState publishers but only weather_solver_node is wired into the launch file. No need to risk a removal touching other call sites today; flagged for a later cleanup slice.
- AFFECTS: `src/core/atmosphere_node/` still compiles but doesn't run in production. New `effective_runway_friction` field has zero-default so the dead publisher is msg-ABI compatible.

Frontend state-machine bugs discovered during end-to-end validation of 5b-iv — fixed as part of making the slice actually work.

- FOUND: Five interacting bugs in `useWeatherV2Store` + `weatherUnits.js` that together produced flaky override propagation and "tab switch forgets settings":
  1. `normalizePatchForDiff` omitted humidity/pressure/runway — `patchesDiffer` returned false for those changes, so `update_patch` was never emitted.
  2. `patchesFromBroadcast` had a stale "DEFERRED" block at the end that overwrote the correctly-parsed wire values with hardcoded defaults (later-keys-win in JS object literals). Every broadcast silently reset the 3 new overrides to false locally.
  3. Dirty-path `syncFromBroadcast` didn't reconcile `patch_id` from server broadcasts — caused duplicate `add_patch` if user kept editing before the ack returned.
  4. Clean-path `syncFromBroadcast` wiped pending-add (`patch_id: null`) patches when a broadcast arrived that didn't include them yet — the "`set_weather` → broadcast, then `add_patch` → broadcast" ordering from backend meant the first broadcast reached the frontend before the new patch was indexed.
  5. Wire builders (`globalDraftToWire`, `applyOverridesToWire`) had inconsistent `??` fallback coverage. Null draft values crashed backend `int(data.get(k, default))` since `.get` with default only kicks in for missing keys, not explicit null.

- DECISION: Fix in place rather than redesigning the draft/accept machinery. All five fixes are additive — no API changes. Together they close the full set of reproducible propagation failures seen during authoring.
- AFFECTS: `useWeatherV2Store.js::normalizePatchForDiff` + `syncFromBroadcast`, `weatherUnits.js::applyOverridesToWire` + `patchesFromBroadcast` + `globalDraftToWire`.

- FOUND: Custom patch coord-based matching (`matchDraftPatch` uses role+lat+lon with 0.0001° tolerance) fails if user moves a patch before its add_patch broadcast ack returns. Broadcast echoes OLD coords with new patch_id; draft already at NEW coords → no match → duplicate on next Accept.
- DECISION: Defer to a future slice. Fix design: frontend-generated UUID + new `client_uuid` field on WeatherPatch.msg; matchDraftPatch matches by UUID first. Airport patches don't hit this bug since ICAO is stable across moves.
- AFFECTS: documented in `backlog.md` ("Stable frontend identity for patches").

- FOUND: SRTM probe failures in `_resolve_ground_elevation_async` return `float('nan')` as ground_elevation_m; patch is added with NaN. `??` in frontend doesn't coalesce NaN (only null/undefined), so NaN propagates into cloud `base_elevation_m = base_agl_ft * FT_TO_M + NaN = NaN` on the wire → weather_solver consumes NaN.
- DECISION: Added `Number.isFinite()` guards in both `applyOverridesToWire` and `patchesFromBroadcast` so NaN is coalesced to 0 at the wire boundaries. Backend-side rejection of failed SRTM probes deferred — out of scope for this session.
- AFFECTS: `weatherUnits.js` (patchGroundM guard + finiteOr helper in broadcast parser).

## 2026-04-23 — Claude Code (CIGI 3.3 opcode audit — host↔plugin alignment)

- DECIDED: Align all CIGI packet IDs emitted by cigi_bridge and xplanecigi
  with the opcode values defined in CIGI 3.3 ICD §2.2.2 Table 1. Three
  opcodes were previously non-standard because matching non-standard
  values on both sides had cancelled out on the wire:

  1. Entity Control:          0x03 → **0x02**  (§4.1.2, Host→IG, 48 B)
  2. Start of Frame (SOF):    0x01 → **0x65**  (§4.2.1, IG→Host, 32 B)
  3. HAT/HOT Response path:   0x66 + custom 48-byte payload
                              → **0x67** (HAT/HOT Extended Response,
                              §4.2.3, IG→Host, 40 B)

- REASON: Bugs #14, #20, #21 in bugs.md. Each was a symmetric bug —
  xplanecigi emitted an incorrect opcode and cigi_bridge parsed that
  same incorrect opcode, so neither side ever failed against the other.
  The flaw only shows up against a compliant third-party IG or CCL stack,
  and it blocks any spec-compliant captured-traffic review. Fixing now
  avoids shipping a simulator that can't interoperate with CCL/commercial
  IGs. Audit table and findings recorded in the session that closed
  Bugs #14/#20/#21.

- HAT/HOT Response shape change (Option B):
  - 0x66 + 48 B custom → 0x67 + 40 B spec-compliant Extended Response.
  - Layout now matches §4.2.3 exactly: HAT (double @ 8), HOT (double @ 16),
    Material Code (uint32 @ 24), Normal Vector Azimuth (float @ 28),
    Normal Vector Elevation (float @ 32), reserved @ 36-39.
  - `HatHotRequest.Request Type` bit field changed from 1 (HOT only) to
    2 (Extended) — a compliant IG will answer with 0x67 and drive the
    new path end-to-end.
  - Framework surface enum (0..10) carried in the low byte of Material
    Code; host masks it back to `HatHotResponse.surface_type` (uint8).

- FRICTION REMOVED FROM WIRE: `static_friction_factor` and
  `rolling_friction_factor` fields deleted from HatHotResponse.msg.
  Verified end-to-end: flight_model_adapter_node never read them —
  ground friction for JSBSim is derived entirely from YAML tables
  indexed by (surface_type, effective_runway_friction) in
  JSBSimSurfaceWriteback. xplanecigi's `default_friction_for_surface()`
  helper deleted with its single call site.

- NORMAL VECTOR STUBBED: azimuth and elevation floats in the Extended
  Response are currently emitted as 0.0. Deferred to a future
  slope-aware slice (crosswind taxi behaviour, sloped parking ramps).

- CONTACT-POINT COVERAGE: Noted during audit — C172 `gear_points:` in
  `src/aircraft/c172/config/config.yaml` declares only 3 contact points
  (nose, left_main, right_main). cigi_bridge issues one HAT/HOT Request
  per point, so only gear contacts are terrain-probed — no wingtip,
  tail, or prop-strike coverage. Matches JSBSim `<ground_reactions>`
  for C172. Broadening to wingstrike/tail-strike requires paired
  changes in the JSBSim aircraft XML; not in scope for this audit.

- NOTE: This is an opcode/layout audit only. Payload-level audit of
  Entity Control bitfields, Atmosphere Control (0x0A), Weather Control
  (0x0C), and Environmental Region Control (0x0B) byte-accurate semantics
  is a separate future slice; only opcodes were verified here.

- AFFECTS:
  - `src/core/cigi_bridge/` — cigi_host_node.{hpp,cpp},
    hat_request_tracker.{hpp,cpp}.
  - `src/sim_msgs/msg/HatHotResponse.msg` — two fields removed;
    consumers regenerated via `colcon build --symlink-install`.
  - `x-plane_plugins/xplanecigi/XPluginMain.cpp` — HAT/HOT response
    encoder rewritten, `default_friction_for_surface()` deleted, new
    `g_elevation` dataref bound.
  - Deployment: MUST redeploy xplanecigi.xpl to X-Plane/Resources/plugins
    at the same time as the updated cigi_bridge_node; mismatched sides
    drop each other's packets silently (opcode IDs now differ).
  - `bugs.md` — Bugs #20 and #21 moved to Resolved when this audit
    completed.

## 2026-04-23 — Claude Code (Debug: expose effective ground friction on FlightModelState + GearState)

- DECIDED: Surface the two JSBSim friction factors that
  `JSBSimSurfaceWriteback::write_surface()` writes each frame
  (`ground/static-friction-factor`, `ground/rolling_friction-factor`)
  as ROS2 message fields so they can be plotted in PlotJuggler /
  inspected via `ros2 topic echo` without attaching a JSBSim
  property-console.

- REASON: Follow-up to the CIGI 3.3 Option B audit (same day). That
  audit removed `static_friction_factor` / `rolling_friction_factor`
  from the `HatHotResponse.msg` wire because nothing consumed them —
  the authoritative values come from aircraft YAML tables in
  `JSBSimSurfaceWriteback`. But after the removal there was no ROS2
  visibility at all on the *effective* values the FDM actually used.
  Debugging taxi-on-grass vs taxi-on-asphalt behaviour required
  inspecting JSBSim internals directly. Adding them back as
  *diagnostic* fields (on FMS for authority, on GearState for
  convenience) restores that visibility without reinstating the
  vestigial wire fields.

- FIELDS ADDED (forward-compatible, defaults 1.0):
  - `FlightModelState.effective_static_friction`  (float32)
  - `FlightModelState.effective_rolling_friction` (float32)
  - `GearState.effective_static_friction`  (float32)
  - `GearState.effective_rolling_friction` (float32)

- DATA FLOW:
  1. `flight_model_adapter_node` subscribes to `HatHotResponse.surface_type`
     and `AtmosphereState.effective_runway_friction` (unchanged).
  2. Each step, `write_surface()` looks up
     (surface_type × runway_friction) in the YAML
     `GroundFrictionTables` and writes factors into JSBSim properties
     (unchanged behaviour).
  3. `JSBSimAdapter::get_state()` now reads those same properties
     straight back into the two new `FlightModelState` fields.
  4. `sim_gear` already subscribes to `FlightModelState`; it copies
     the two fields verbatim into its published `GearState`.

- ARCHITECTURE: Coupling through `/aircraft/fdm/state` per the
  canonical "no cross-subscribes except through FlightModelState"
  rule in CLAUDE.md. No new subscriptions added anywhere.

- NOT AUTHORITATIVE: These fields are *debug* — they reflect what was
  applied, not what drives the FDM. JSBSim's FGGroundReactions reads
  the same properties internally; they are not an input from ROS2.
  IOS / QTG must not use these fields as canonical ground-friction
  state.

- AFFECTS:
  - `src/sim_msgs/msg/FlightModelState.msg` — 2 fields added.
  - `src/sim_msgs/msg/GearState.msg` — 2 fields added.
  - `src/core/flight_model_adapter/src/JSBSimAdapter.cpp` —
    `get_state()` reads JSBSim friction properties.
  - `src/systems/gear/src/gear_node.cpp` — copies from FMS to
    GearState at publish time.

## 2026-04-23 — Claude Code (Nosewheel steering wired end-to-end)

- DECIDED: Drive JSBSim's `fcs/steer-cmd-norm` from the arbitrated
  rudder channel, and read `gear/unit[i]/steering-angle-deg` back
  into `FlightModelState.wheel_angle_deg[]`. Before this change the
  nose wheel never turned: the command path wrote `fcs/rudder-cmd-norm`
  only, and the readback hardcoded `wheel_angle_deg[i] = 0.0f`.

- REASON: Observed during GearState-publish debugging (same day) —
  nosewheel-angle field stuck at 0 on the wire regardless of pedal
  input. Tracing showed (a) `flight_model_adapter_node.cpp:388-390`
  writes rudder-cmd-norm but not steer-cmd-norm; c172p.xml's FCS Yaw
  channel is aero-only — no internal rudder→steer tie, so JSBSim's
  FGLGear never received a steering command. (b) `JSBSimAdapter.cpp`
  in the gear section hardcoded `wheel_angle_deg = 0.0f` instead of
  reading back the per-bogey steering angle. Both bugs latent since
  the aircraft first ran on JSBSim — ground handling with pedals had
  never actually been exercised on C172 before today.

- DATA FLOW:
  1. Rudder pedals → input_arbitrator → `/aircraft/controls/flight`
     as `FlightControls.rudder_norm` (unchanged).
  2. `flight_model_adapter_node` (step 386-404) now writes BOTH
     `fcs/rudder-cmd-norm` (aero rudder, unchanged) and
     `fcs/steer-cmd-norm` (ground steering, new) from the same
     `rudder_norm` value.
  3. JSBSim's FGLGear multiplies steer-cmd-norm by each bogey's
     `<max_steer>` (c172p.xml: nose=10°, mains=0°, tail=0°) and
     sets `gear/unit[i]/steering-angle-deg` each frame.
  4. `JSBSimAdapter::get_state()` reads `steering-angle-deg` per
     bogey into `FlightModelState.wheel_angle_deg[i]`.
  5. `sim_gear` already copies `wheel_angle_deg[0]` into
     `GearState.nosewheel_angle_deg` (unchanged — was just seeing 0
     before this fix). IOS / cockpit displays now see live steering.

- DESIGN CHOICE: Direct pass-through rudder_norm → steer-cmd-norm
  with no gain or speed scheduling. Matches C172's real mechanical
  rudder↔nosewheel spring link. Per-aircraft steering authority stays
  in the JSBSim XML (via `<max_steer>`), not in adapter code. If
  ground handling feels twitchy, gain/schedule tuning is a per-
  aircraft YAML extension, not an adapter hack.

- AIRCRAFT COVERAGE: Works for any aircraft whose JSBSim XML defines
  nonzero `<max_steer>` on some bogey (default pattern for tricycle
  gear). Aircraft that don't (tailwheel types, full helicopter with
  skids) write to `fcs/steer-cmd-norm` harmlessly — nothing consumes
  it. No plugin or aircraft-id conditionals needed in the adapter.

- AFFECTS:
  - `src/core/flight_model_adapter/src/flight_model_adapter_node.cpp`
    — 1 line added (steer-cmd-norm writeback).
  - `src/core/flight_model_adapter/src/JSBSimAdapter.cpp` — replace
    hardcoded zero with per-bogey steering-angle-deg readback.

## 2026-04-23 — 15:42:49 - Claude Code

- DECIDED: `cigi_bridge` host-side wire format is now spec-conformant
  CIGI 3.3 (§4.1.1, §4.1.24, §4.2.1), verified by a CCL round-trip
  regression test (`test_cigi_wire_conformance`) that runs on every
  commit when CCL is installed.

- REASON: Audit on 2026-04-23 against Boeing's CCL and the Wireshark
  dissector showed three critical wire-format bugs in
  `cigi_host_node.cpp`:
    * IG Control encoder missing Major Version and Byte Swap Magic
      Number, with IG Mode at the wrong byte and Timestamp encoded
      as a double at byte 16 instead of uint32 at byte 12.
    * HAT/HOT Request encoder placing Lat/Lon/Alt at offsets
      12/20/28 (the last as float) instead of the spec-mandated
      8/16/24 (all doubles).
    * SOF parser reading IG Mode from byte 4 (IG Status Code) rather
      than byte 5 bits 1..0 (HDta bitfield).
  These bugs were masked because the X-Plane plugin
  (`x-plane_plugins/xplanecigi/`) was co-developed against the same
  non-conformant layout. After 35 commits iterating on
  `src/core/cigi_bridge/` without ever pinning to the spec, the
  accumulated drift became hard to debug.

- FIX: Realigned both sides (host + plugin) to spec in the same
  branch, added a CCL round-trip test. The test is the new
  foundation — if the wire format drifts from spec, the test fails
  at build time and the build is broken until fixed.

- IG Mode values: framework uses `STANDBY = 0x24`, `RESET = 0x24`
  (alias — spec collapses Reset/Standby to wire value 0), and
  `OPERATE = 0x25`. The X-Plane plugin's terrain-probe stability
  trigger moved from Reset→Operate (1→2 in old dialect) to
  Standby→Operate (0→1 in spec) — same semantic event.

- AFFECTS:
  - `src/core/cigi_bridge/include/cigi_bridge/cigi_host_node.hpp`
    (IG Mode constants, last_ig_frame_ state for SOF echo).
  - `src/core/cigi_bridge/src/cigi_host_node.cpp`
    (three encoders/parser rewritten).
  - `src/core/cigi_bridge/CMakeLists.txt`
    (CCL search paths extended, round-trip test target added,
    vendored cigi_ig_interface gated behind
    `-DBUILD_CIGI_IG_INTERFACE=ON`).
  - `src/core/cigi_bridge/test/test_cigi_wire_conformance.cpp` (new).
  - `x-plane_plugins/xplanecigi/XPluginMain.cpp` (parsers + SOF
    encoder realigned to spec; mode-transition trigger updated).

- FOLLOW-UP: Run the end-to-end smoke test with X-Plane connected
  to confirm the coordinated host+plugin change works as expected
  before merging `cigi-spec-conformance` → `main`. After that,
  install CCL on CI so the round-trip test runs automatically.
  Optional future work: retire the X-Plane plugin's hand-written
  CIGI encode/decode in favour of a CCL-based one under
  `~/CIGI_IG_Interface`.

## 2026-04-23 — Claude Code

- DECIDED: Retire all hand-rolled CIGI 3.3 wire handling on both
  sides. Host node and X-Plane plugin now route every outbound and
  inbound packet through a shared `cigi_session` library that wraps
  Boeing's CCL 3.3.3 (`CigiHostSession`/`CigiIGSession`).
- REASON: The prior hand-rolled encoders wrote big-endian unconditionally
  and the plugin parsed big-endian unconditionally — compatible by
  accident, but non-standard (CIGI actually specifies "sender-native
  byte order plus Byte Swap Magic 0x8000 for the recipient to detect
  swap"). Every attempt to extend the catalog (weather, runway friction,
  future CGF/SAF/entities) required re-auditing byte offsets against the
  ICD. Using CCL on both sides makes the wire format spec-conformant by
  construction and adding new packet types stops requiring byte-level
  audits.
- AFFECTS:
  - Created `src/core/cigi_bridge/cigi_session/` — HostSession,
    IgSession, 7 processor interfaces, 15 gtest round-trip cases.
    Linked from both the ROS2 host node (Linux native) and the plugin
    (mingw64 cross-compile) via CCL built at
    `references/CIGI/cigi3.3/ccl/install/` + `install-mingw/`.
  - Rewrote `src/core/cigi_bridge/src/cigi_host_node.cpp`:
    `send_cigi_frame` assembles one datagram via
    `session_.BeginFrame/Append*/FinishFrame`; `recv_pending` calls
    `session_.HandleDatagram` and dispatches SOF / HAT-HOT-response
    through `ISofProcessor` / `IHatHotRespProcessor`.
  - Rewrote `x-plane_plugins/xplanecigi/XPluginMain.cpp`: plugin
    implements all 7 IgSession processor interfaces; `send_sof` and
    HAT/HOT Extended Response emission go through `IgSession::BeginFrame` /
    `AppendHatHotXResp`.
  - Deleted `weather_encoder.cpp`/`.hpp`, `test_weather_encoder.cpp`,
    `test_weather_sync.cpp`, `test_cigi_wire_conformance.cpp`, and
    the plugin's `read_be*`/`write_be*` helpers. CCL round-trip tests
    under `cigi_session/test/` replace the conformance suite.
  - Runway Friction previously used a non-standard `0xCB` packet;
    now rides standard Component Control (Class=GlobalTerrainSurface,
    ID=100, State=friction 0..15). Every CCL-based IG can dispatch it
    without a custom handler.
  - HOT Requests now ride a second session-owned datagram (required
    because CIGI 3.3 §4.1.1 mandates IG Control as the first packet of
    every Host→IG datagram; previous code sent HOT requests as
    headerless datagrams).
  - CCL is now a hard dependency of `sim_cigi_bridge`
    (`FATAL_ERROR` if not found).

- FOLLOW-UP: Run the end-to-end smoke test with X-Plane connected to
  confirm runway friction, HAT/HOT, visibility, clouds, and
  repositioning all still work after the big-bang migration; then
  merge `cigi-spec-conformance` → `main`. Adding new packet types
  (CGF, SAF, additional entities) now means adding one
  `Append<Packet>` method on `HostSession` and one processor
  interface — no byte-offset work.


## 2026-04-23 — Claude Code (smoke-test follow-up)

- DECIDED: Session-append library API (`HostSession::BeginFrame /
  Append* / FinishFrame`) instead of the plan's originally specified
  free-function `BuildXxx(buf, cap, ...)` emitters.
- REASON: Discovered during Task 1.3 that individual CCL `Pack()`
  methods write host byte order — not wire format — and that wire-
  format output is only produced by `CigiOutgoingMsg`, which enforces
  "first packet must be IG Control (host) or SOF (IG)" via
  `CigiMissingIgControlException` / `CigiMissingStartOfFrameException`.
  A single-packet emitter function cannot satisfy that invariant, so
  the library was restructured around the session's native assembly
  pattern. `HostSession` / `IgSession` each own a `CigiHostSession` /
  `CigiIGSession` and expose `BeginFrame` to append the header packet,
  typed `Append*` methods per payload class, and `FinishFrame` to
  `PackageMsg` the datagram. Round-trip tests (15 cases) replace the
  per-packet emitter tests from the plan.
- AFFECTS:
  - Library API under `src/core/cigi_bridge/cigi_session/` matches the
    above pattern.
  - Phase 2/3 task execution reordered: library built out completely
    (Phase 1+2+3) with no host/plugin integration, then atomic
    migration committed both sides simultaneously (commits `c6050d1`
    + `162ab91`). `test_cigi_wire_conformance` was deleted in the
    same migration because its BE-only contract was obsolete.

- DECIDED: Static-link CCL for the host node.
- REASON: Building CCL produced `libcigicl.so.1` at
  `references/CIGI/cigi3.3/ccl/install/lib/` — outside any system ld
  path, so `cigi_bridge_node` failed to start with "cannot open
  shared object file". The CCL build also produced `libcigicl.a`;
  linking the static archive removes the runtime .so dependency.
- AFFECTS: `src/core/cigi_bridge/CMakeLists.txt` pushes `.a` to the
  front of `CMAKE_FIND_LIBRARY_SUFFIXES` before `find_library`.

- DECIDED: Call `CigiOutgoingMsg::FreeMsg()` at the start of each
  `BeginFrame`, and wrap `CigiIncomingMsg::ProcessIncomingMsg()` in
  try/catch inside every `HandleDatagram`.
- REASON: CCL's buffer queue leaves the just-packaged buffer locked
  at the head of the queue — the next `PackageMsg` call finds that
  locked buffer and throws `CigiCalledOutOfSequenceException`. Without
  `FreeMsg`, the node crashed on frame 2. Separately,
  `ProcessIncomingMsg` throws on malformed datagrams (missing header,
  buffer overrun, unknown version); in the X-Plane plugin those
  exceptions escaped the UDP handler and surfaced as "Forwarding
  exception to previous handler" in the plugin log. Wrapping both
  sides in try/catch contains the blast radius of a single bad packet.
- AFFECTS:
  - `HostSession::BeginFrame` and `IgSession::BeginFrame` now call
    `FreeMsg()` if a previous frame was packaged.
  - `HostSession::HandleDatagram` and `IgSession::HandleDatagram` now
    return 0 on any caught CCL exception instead of propagating.

- SMOKE TEST: End-to-end run (sim + X-Plane + IOS) passed 2026-04-23.
  HAT/HOT, visibility, clouds, and runway friction all work over the
  new CCL-based path.

- MERGED: `cigi-spec-conformance` → `main` fast-forward (`7987767`
  → `539d285`), pushed to `origin/main`. 30 commits landed:
  library scaffold (4), host-side emitters/processors (8), IG-side
  session + processors (6), plugin CMake + atomic host+plugin
  migration (3), decisions doc (1), runtime fixes (2), pre-existing
  pre-Phase-1 spec/plan commits (6).
