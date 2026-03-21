# Repositioning Pipeline Design

**Date:** 2026-03-22
**Status:** Approved design, pending implementation

## Problem

When repositioning the aircraft via the IOS, several issues interact:

1. JSBSim's `force-on-ground` only works in RUNNING state — ICs are applied in READY
2. CIGI HOT rate gating stops terrain requests when AGL is wrong after repositioning
3. X-Plane needs time to page terrain tiles at the new position
4. JSBSim's `RunIC()` resets the `terrain-elevation-asl-ft` property

The result: aircraft ends up underground, terrain source stuck on SRTM, visual/physics mismatch.

## Design

### Instructor experience

Sequenced repositioning: click SET POSITION, IOS shows "REPOSITIONING" for 1-3 seconds while terrain resolves, then aircraft appears at correct height. Clean, no visual glitch.

### State machine

Add `STATE_REPOSITIONING = 6` to `SimState.msg`.

Valid transitions:
- READY → REPOSITIONING (on IC received)
- RUNNING → REPOSITIONING (on IC received)
- FROZEN → REPOSITIONING (on IC received)
- REPOSITIONING → READY (terrain resolved)

System nodes treat REPOSITIONING same as FROZEN (hold state, keep publishing).
IOS status strip shows "REPOSITIONING" label.
Clock paused during REPOSITIONING.

### Repositioning sequence

```
IOS "SET POSITION" (double-tap confirm)
        │
        ▼
sim_manager → STATE_REPOSITIONING
        │
        ▼
IC published on /sim/initial_conditions
        │
        ▼
flight_model_adapter:
  1. Apply raw IC (entity moves to target lat/lon)
  2. Clear stale terrain data
  3. Wait for terrain...
        │
        ├── CIGI path (IG connected):
        │     cigi_bridge sends Entity Control (0x03) to IG
        │     cigi_bridge sends 3 HOT requests at gear points (no rate gating)
        │     X-Plane plugin:
        │       - Receives Entity Control → detects position jump
        │       - Shows "REPOSITIONING..." overlay on screen
        │       - Monitors sim/graphics/scenery/async_scenery_load_in_progress
        │       - Sends SOF with IG Status = Standby (0) while loading
        │       - When load complete (debounced 3 frames): IG Status = Operate (1)
        │       - Hides overlay
        │     cigi_bridge reads SOF → IG Status = Operate
        │     flight_model_adapter receives 3 HOT responses
        │     Compute ground plane from 3 gear contact points
        │     Set terrain + altitude (terrain + gear z_m offset)
        │     Publish /sim/terrain/ready = true
        │
        └── SRTM path (no IG):
              SRTM service query (async, ~50ms)
              Set terrain + altitude (SRTM + gear z_m offset)
              Publish /sim/terrain/ready = true
        │
        ▼
sim_manager receives /sim/terrain/ready = true
        │
        ▼
sim_manager → STATE_READY
```

### Timeout

- **2s** — if no CIGI HOT, accept SRTM
- **5s** — if nothing at all, use raw IC altitude + warn
- sim_manager transitions to READY regardless at timeout

### Terrain height computation

**With CIGI (3 gear HOTs):**
- HOT returns terrain MSL at each gear contact position
- Average of 3 HOT elevations = terrain under aircraft
- CG altitude = average terrain + abs(max gear z_m from config.yaml)
- Future enhancement: compute pitch/roll from 3-point ground plane for sloped terrain

**Without CIGI (SRTM):**
- Single elevation point at aircraft lat/lon
- CG altitude = SRTM elevation + abs(max gear z_m from config.yaml)
- Gear z_m loaded from aircraft config.yaml `gear_points` section

### X-Plane plugin changes (XPluginMain.cpp)

**New: Entity Control (0x03) handler**
- Parse lat/lon/alt (float64 BE) and attitude (float32 BE) from packet
- Convert to X-Plane local coordinates via `XPLMWorldToLocal`
- Position ownship via `XPLMSetDatad` on `sim/flightmodel/position/*` datarefs
- Detect large position change (>0.01 deg lat or lon)
- On change: set `repositioning_ = true`

**New: scenery load monitoring**
- Dataref: `sim/graphics/scenery/async_scenery_load_in_progress` (int, read-only)
- While 1: IG is loading terrain tiles
- When 0 for 3+ consecutive frames: terrain is ready
- On ready: set `repositioning_ = false`

**New: SOF (Start of Frame) response**
- Send after each received datagram
- 32 bytes, CIGI 3.3 format
- IG Status field (byte 2, bits 0-1):
  - 0 = Standby (loading / repositioning)
  - 1 = Operate (ready)
- Mirror host frame counter from IG Control packet

**New: repositioning overlay**
- Register `XPLMRegisterDrawCallback` for screen overlay
- While `repositioning_ == true`: draw "REPOSITIONING..." in amber
- Uses `XPLMDrawString` — no window, no click handling, just text overlay

### cigi_bridge changes

- Parse SOF packets (0x01) from IG response datagrams
- Track IG status internally
- During REPOSITIONING state: send HOT requests for all 3 gear points every frame (bypass rate gating)
- Normal operation: existing rate gating by AGL

### flight_model_adapter changes

- On IC: apply raw IC, clear terrain_hot_, set pending state
- After RunIC: set terrain property (RunIC resets it)
- Compute CG height from gear_points z_m in config.yaml (loaded at configure time)
- Publish `/sim/terrain/ready` (std_msgs/Bool) when terrain is resolved
- SRTM fallback if no CIGI within 2s

### sim_manager changes

- New state: STATE_REPOSITIONING
- On IC received (any active state): transition to REPOSITIONING
- Subscribe to `/sim/terrain/ready`
- In REPOSITIONING: clock paused, wait for ready signal
- Tiered timeout: 2s CIGI, 5s total
- On ready or timeout: transition to READY

### New messages

- `SimState.msg`: add `uint8 STATE_REPOSITIONING = 6`
- `/sim/terrain/ready`: `std_msgs/Bool` — published by flight_model_adapter

### Files affected

| File | Change |
|---|---|
| `sim_msgs/msg/SimState.msg` | Add STATE_REPOSITIONING = 6 |
| `src/core/sim_manager/src/sim_manager_node.cpp` | REPOSITIONING state, terrain ready sub, timeout |
| `src/core/flight_model_adapter/src/flight_model_adapter_node.cpp` | IC sequence, terrain ready pub, gear height from config |
| `src/core/cigi_bridge/src/cigi_host_node.cpp` | SOF parsing, HOT rate gating bypass |
| `~/x-plane_plugins/xplanecigi/XPluginMain.cpp` | Entity Control, SOF, scenery load, overlay |
| `ios/frontend/src/store/useSimStore.js` | Handle REPOSITIONING state |
| `ios/frontend/src/components/StatusStrip.jsx` | REPOSITIONING badge |
| `launch/sim_full.launch.py` | No change needed |

### What doesn't change

- System nodes: treat REPOSITIONING same as FROZEN
- controls.html: unaffected
- Aircraft configs: gear_points already defined, no new fields needed (remove gear_cg_offset_m added this session)
- IOS panels: work normally, just frozen during REPOSITIONING
