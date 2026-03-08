# DECISIONS.md
# Append-only log of structural decisions.
# Both Claude Code and Claude Chat use this as a shared record.
#
#
# FORMAT:
#   ## YYYY-MM-DD — [tool: Claude Chat | Claude Code]
#   - DECIDED: what was locked in
#   - REASON: why (one line)
#   - AFFECTS: which files/nodes/topics
#
# RULES:
#   - Never delete or edit past entries
#   - Claude Code: append an entry whenever you make a structural decision
#     (new message type, changed topic name, deferred something, refactored a node)
#   - Claude Chat: paste this file at the start of each design session
# ─────────────────────────────────────────────────────────────────────────────

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
