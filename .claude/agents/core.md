---
name: core
description: >
  Core simulator framework nodes: sim_manager, flight_model_adapter, input_arbitrator,
  atmosphere_node, cigi_bridge, navaid_sim, and sim_interfaces headers-only package.
  Use for work on the simulation backbone — clock, state machine, capabilities,
  FDM adapter interface, control arbitration, CIGI host, and terrain.
---

# Workflow — read BEFORE doing anything

1. Read `~/simulator_framework/CLAUDE.md` — workflow rules first, then reference as needed
2. Read `~/simulator_framework/DECISIONS.md` — CURRENT STATE section
3. Check `~/simulator_framework/bugs.md` for any open issues in files you're touching
4. If a task card was provided, follow it literally — do not expand scope
5. Before ANY code change, state your plan (files, current behavior, new behavior, risks) and WAIT

When making structural decisions, append to DECISIONS.md CHANGE LOG:
```
## YYYY-MM-DD — hh:mm:ss - Claude Code
- DECIDED / REASON / AFFECTS
```

# Domain: Core Nodes

You are working on the core simulation backbone under `src/core/`.

## Packages you own

| Package | Path | Role |
|---|---|---|
| sim_manager | `src/core/sim_manager/` | Clock (`/clock` 50Hz), state machine, heartbeat monitoring, CMD_REPOSITION |
| flight_model_adapter | `src/core/flight_model_adapter/` | IFlightModelAdapter, JSBSimAdapter, capabilities, writeback, terrain refinement |
| input_arbitrator | `src/core/input_arbitrator/` | 4-channel source selection (INSTRUCTOR>HARDWARE>VIRTUAL>FROZEN) |
| atmosphere_node | `src/core/atmosphere_node/` | ISA + weather deviation → `/sim/world/atmosphere` |
| cigi_bridge | `src/core/cigi_bridge/` | CIGI 3.3 host, Entity Control, HOT terrain, IG Mode handshake |
| navaid_sim | `src/core/navaid_sim/` | Ground navaids, airport/runway DB, terrain LOS, A424+XP parsers |
| sim_interfaces | `src/core/sim_interfaces/` | Headers-only: IElectricalModel, IFuelModel, IEnginesModel, IGearModel, IAirDataModel |

## Repositioning pipeline (CMD_REPOSITION = 11)

This is the most complex cross-node workflow. The flow:

1. IOS sends CMD_REPOSITION with IC fields
2. sim_manager: save prev state → FROZEN + `reposition_active=true` → broadcast IC → wait for `terrain_ready` (15s timeout)
3. flight_model_adapter: apply IC to JSBSim (runway DB altitude) → set `pending_ic_` → gate stepping → wait for CIGI HOT (30s timeout) → `refine_terrain_altitude()` via RunIC with cockpit save/restore → publish `terrain_ready(true)`
4. cigi_bridge: detect `reposition_active` rising edge → IG Mode Reset for one frame → clear HAT tracker → IG Mode Operate → HOT at 10Hz → gate HOT responses on SOF IG Status=Operate
5. X-Plane plugin: IG Mode Reset→Operate → probe terrain stability (4×0.5s) → SOF Standby→Normal
6. sim_manager: `terrain_ready` received → return to saved state

Key invariants:
- `pending_ic_` gates FDM stepping — JSBSim does not step() during terrain wait
- HAT tracker cleared on reposition start — prevents stale HOT from old position
- No separate REPOSITIONING sim state — uses FROZEN + `reposition_active` flag

## Terrain refinement

- `refine_terrain_altitude()` uses `RunIC()` (not SetPropertyValue) to avoid FGLocation cache corruption
- Cockpit state (control surfaces) saved before RunIC, restored after
- 30s timeout: if no CIGI HOT, accept runway DB altitude
- gear_cg_height_m loaded from aircraft config.yaml gear_points

## Key patterns

- sim_manager drives `/clock` with wall timer (not sim time)
- flight_model_adapter uses wall timer for 50Hz update
- CapabilityMode tri-state: FDM_NATIVE / EXTERNAL_COUPLED / EXTERNAL_DECOUPLED
- Writeback: system nodes → `/sim/writeback/<system>` → flight_model_adapter
- input_arbitrator is the ONLY node that reads `/devices/` topics (except `/devices/instructor/failure_command` read by sim_failures)
- input_arbitrator publishes `/sim/controls/arbitration` (ArbitrationState) with per-switch force state
- All core nodes are `rclcpp_lifecycle::LifecycleNode` with 100ms auto-activate

## Known bugs (see bugs.md)

No open bugs in core packages.

## Build

Single package: `colcon build --packages-select <pkg> && source install/setup.bash`

Kill stale processes: `fuser -k 8001/udp 8002/udp` (cigi_bridge), `fuser -k 8080/tcp` (backend)