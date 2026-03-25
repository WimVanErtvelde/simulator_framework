# REPOSITION DESIGN — CIGI 3.3 Compliant
# Date: 2026-03-22
# Status: APPROVED — ready for implementation
# Save to: DECISIONS.md (append) and use as implementation spec

## Overview

Repositioning moves the aircraft to a new lat/lon/heading while the sim is frozen.
Two-stage altitude: runway DB elevation for instant placement, CIGI HOT for precision refinement.
Fully CIGI 3.3 compliant — works with any IG, not just X-Plane.

## Architecture Principles

1. **sim_manager owns the reposition workflow** — no REPOSITIONING state, uses FROZEN + reposition_active flag
2. **RunIC() is called exactly ONCE per reposition** with the best available altitude
3. **CIGI protocol drives terrain readiness** — Host controls IG Mode (Reset/Operate), IG reports readiness via SOF (Standby/Normal)
4. **Two-stage altitude**: runway DB elevation (instant, ±1m) → CIGI HOT per gear point (precision, ±0.01m)
5. **No-CIGI fallback** for development: runway DB elevation or SRTM from navaid_sim GetTerrainElevation service

## Data Flow

```
Instructor selects runway position icon on IOS POS page
        │
        ▼
Frontend builds reposition data:
  lat_rad, lon_rad     ← from runway threshold (GetRunways service)
  heading_rad          ← from runway heading
  alt_m                ← runway elevation_m + position icon offset (RWY=0, 2NM=pattern alt, etc.)
  airspeed_ms          ← 0 for ground, pattern speed for airborne
  configuration        ← ready_for_takeoff or airborne_clean
        │
        ▼
Frontend sends WS: { type: 'set_departure', lat_rad, lon_rad, alt_m, heading_rad, airspeed_ms }
        │
        ▼
IOS backend: publishes SimCommand CMD_REPOSITION=11 with payload_json
        │
        ▼
sim_manager: CMD_REPOSITION handler
  ├─ save pre_reposition_state_ (RUNNING, FROZEN, or READY)
  ├─ transition to FROZEN (if not already)
  ├─ set reposition_active = true
  ├─ parse IC from payload_json → update current_ic_
  ├─ broadcast IC on /sim/initial_conditions
  ├─ publish SimState with reposition_active=true
  ├─ start reposition timeout (120s configurable)
        │
        ▼
flight_model_adapter: IC subscription handler
  ├─ clear terrain_hot_
  ├─ publish terrain_ready(false)
  ├─ apply_initial_conditions(*msg) → ONE RunIC()
  │   Aircraft placed at runway DB altitude (±1m accuracy)
  │   Good enough to be on the runway surface
  ├─ set pending_ic_ for terrain refinement
  ├─ cache lat/lon from IC for FlightModelState output
  │   (prevents RunIC geodetic drift from reaching cigi_bridge)
        │
        ▼
cigi_bridge: reads reposition_active from SimState
  ├─ sends IG Control with IG Mode = RESET (one frame)
  ├─ sends IG Control with IG Mode = OPERATE (subsequent frames)
  ├─ sends Entity Control with new lat/lon/alt from FlightModelState
  ├─ sends HOT requests at 10Hz for all gear points
        │
        ▼
X-Plane plugin: receives IG Mode
  ├─ RESET received: set internal flag, SOF IG Status = Standby
  ├─ OPERATE received: begin terrain probe stability check
  │     probe at entity lat/lon every 0.5s (wall clock)
  │     require 4 consecutive probes within 1.0m tolerance
  │     during probing: SOF IG Status = Standby
  ├─ probes stable: SOF IG Status = Normal
  │   (X-Plane tile load may take 1-60 seconds — that's fine)
        │
        ▼
cigi_bridge: receives SOF IG Status = Normal
  ├─ ig_status_ = Normal (2)
  ├─ publishes HOT responses (IG gate passes: ig_status_ == 2)
        │
        ▼
flight_model_adapter: HOT response handler
  ├─ accumulates terrain_hot_[point_name] = hot for each gear point
  ├─ if pending_ic_ is set:
  │     compute terrain_m from HOT data
  │     call refine_terrain_altitude(corrected_alt, terrain_m)
  │       → sets position/terrain-elevation-asl-ft (property write, safe)
  │       → sets position/h-sl-ft to terrain + gear_cg_height (property write)
  │       → NO RunIC — position stays exactly where it is
  │     clear pending_ic_
  │     clear position cache (JSBSim position is now authoritative)
  │     publish terrain_ready(true)
        │
        ▼
sim_manager: terrain_ready(true) received
  ├─ clear reposition_active
  ├─ publish SimState with reposition_active=false
  ├─ transition to pre_reposition_state_
  ├─ Done. Instructor is back where they were.
```

## CIGI Protocol Compliance

### Host → IG (IG Control packet, byte 3 bits 1:0)
- **Reset (0)**: Sent for ONE frame when reposition_active transitions true.
  Tells IG to prepare for position change.
- **Operate (1)**: Sent on all subsequent frames.
  Tells IG to go live when ready.

### IG → Host (SOF packet, byte 4 bits 1:0)
- **Standby (0)**: IG is loading terrain, not ready for HOT probes.
- **Normal/Operate (2)**: IG has valid terrain, HOT probes return correct data.

### HOT Request/Response
- Host sends HOT Request (0x18) for each gear point at 10Hz during FROZEN
- IG responds with HOT Response (0x02) — terrain elevation MSL at each point
- Host ONLY trusts HOT when SOF IG Status = Normal (gate in cigi_bridge)

### Compliance notes
- We never send IG Mode = Standby (that's IG→Host only)
- Any CIGI 3.3 compliant IG should handle Reset→Operate transition
- HOT responses are position-specific — IG probes terrain at the lat/lon in the request
- No proprietary extensions or custom packets used

## X-Plane Plugin Changes

### Remove custom reposition detection
- Remove: g_repositioning flag based on position delta detection
- Remove: frame-based or time-based probe stability from FlightLoopCallback
- Remove: custom "REPOSITIONING..." overlay

### Add IG Mode handling
- Parse IG Mode from IG Control packet byte 3 bits 1:0
- On Reset (0): set g_ig_mode = RESET, SOF IG Status = Standby
- On Operate (1): set g_ig_mode = OPERATE
  If transitioning from RESET: begin terrain probe stability check
  If already OPERATE (normal operation): SOF IG Status = Normal

### Terrain readiness (probe-based)
- Only runs after RESET → OPERATE transition
- Probe at entity position every 0.5s (wall clock, XPLMGetElapsedTime)
- 4 consecutive probes within 1.0m = terrain stable
- During probing: SOF IG Status = Standby
- After stable: SOF IG Status = Normal
- Normal flight (no recent RESET): SOF IG Status = Normal always

### Plugin becomes simpler
- No position-delta detection
- No frame counting
- Host explicitly tells plugin when reposition happens (via Reset)
- Plugin just reports terrain readiness (via SOF)

## cigi_bridge Changes

### Read reposition_active from SimState
- state_sub_ callback: store both msg->state and msg->reposition_active
- Use reposition_active to drive IG Mode transitions

### IG Mode in IG Control
- When reposition_active transitions false→true: send IG Mode = Reset for 1 frame
- All other frames: send IG Mode = Operate
- Encode in existing encode_ig_ctrl() function, byte 3

### HOT rate gating
- FROZEN + reposition_active: send HOT at 10Hz (terrain loading)
- FROZEN + !reposition_active: send HOT at normal rate (instructor freeze)
- RUNNING: existing AGL-based rate gating (unchanged)

### HOT response gating
- Only publish HOT responses when ig_status_ == 2 (Normal)
- This is already implemented

## flight_model_adapter Changes

### IC handler (on /sim/initial_conditions)
- clear terrain_hot_
- publish terrain_ready(false)
- apply_initial_conditions(*msg) — ONE RunIC with runway DB altitude
- set pending_ic_ for refinement
- cache IC lat/lon for FlightModelState output (prevents RunIC geodetic drift)

### HOT handler
- Accumulate terrain_hot_ per gear point
- When pending_ic_ set and HOT data available:
    - compute terrain_m (use average of all received HOT points)
    - refine_terrain_altitude: property writes only (h-sl-ft + terrain-elevation-asl-ft)
    - NO RunIC — preserves exact position
    - clear pending_ic_ and position cache
    - publish terrain_ready(true)

### pending_ic_ timeout
- If pending_ic_ has been set for >5 seconds with no valid HOT:
    - Accept the runway DB altitude (already applied by RunIC)
    - Clear pending_ic_
    - Publish terrain_ready(true)
    - Log warning: "terrain refinement timeout — using runway DB altitude"
- This covers no-CIGI development and IG failure scenarios

### refine_terrain_altitude (JSBSimAdapter)
- Set position/h-sl-ft via SetPropertyValue (calls FGPropagate::SetAltitudeASL)
- Set position/terrain-elevation-asl-ft
- NO RunIC, no IC object manipulation, no force-on-ground
- Note: h-sl-ft property write may cause FGLocation cache issue — if position
  drifts, use the cached IC position in FlightModelState output until next step()

### update_terrain_elevation (continuous, non-IC)
- Only runs when pending_ic_ is NOT set
- Takes FlightModelState as parameter (no separate get_state() call)
- Sets terrain-elevation-asl-ft only (not h-sl-ft)
- Guard: terrain must be below aircraft altitude

## sim_manager Changes

### CMD_REPOSITION handler
- Guard: reject in INIT, SHUTDOWN, RESETTING states
- Save pre_reposition_state_
- Transition to FROZEN (READY→FROZEN now valid)
- Set reposition_active = true
- Parse IC from payload_json
- Broadcast IC
- Start 120s timeout (configurable via parameter)

### finish_reposition
- Clear reposition_active
- Transition to pre_reposition_state_
- Check node health before returning to RUNNING

### SimState message
- bool reposition_active published with every state message
- IOS frontend checks this for "REPOSITIONING..." badge and button lockout

### CMD_RUN/FREEZE/UNFREEZE during reposition
- Rejected with log warning when reposition_active is true

## IOS Changes

### Backend (ios_backend_node.py)
- Forward reposition_active from SimState to WebSocket clients
- CMD_REPOSITION uses _cmd_pub (not _command_pub)
- Hardcoded 11 should use SimCommand.CMD_REPOSITION constant

### Frontend (PositionPanel.jsx)
- Already sends alt_m from runway DB elevation ← CONFIRMED WORKING
- Show "REPOSITIONING..." badge when simState.reposition_active is true
- Lock RUN/FREEZE/RESET buttons during reposition

## Timeout Strategy

| Scenario | Timeout | Action |
|----------|---------|--------|
| FMA pending_ic_ (no HOT) | 5s | Accept runway DB altitude, publish terrain_ready |
| sim_manager reposition | 120s | Force finish_reposition, log warning |
| IOS display | None | Show "REPOSITIONING..." until complete |

## No-CIGI Development Fallback

When X-Plane/CIGI is not connected:
1. RunIC places aircraft at runway DB altitude (already correct to ±1m)
2. No HOT arrives → pending_ic_ times out at 5s
3. terrain_ready published → reposition completes
4. Aircraft is on the runway at DB altitude. Good enough for IOS/systems development.

Optional enhancement: query navaid_sim GetTerrainElevation service as intermediate fallback
(SRTM data, ±30m accuracy). Only if runway DB altitude is not available (manual lat/lon entry).

## Implementation Order

1. Fix remaining bugs from bug report (format string, dead code, etc.)
2. cigi_bridge: add IG Mode Reset/Operate control based on reposition_active
3. X-Plane plugin: remove custom reposition detection, add IG Mode handling
4. flight_model_adapter: add pending_ic_ timeout (5s)
5. IOS: forward reposition_active to frontend, add badge/lockout
6. Update CLAUDE.md and DECISIONS.md with current architecture
7. Test: same-airport reposition, cross-continent reposition, no-CIGI reposition
