---
name: ios
model: sonnet
description: >
  IOS (Instructor Operating Station) — both backend (FastAPI + rclpy) and frontend
  (React + Zustand + WebSocket). Use for work on the instructor station UI, WebSocket
  protocol, ROS2 bridge, virtual cockpit pages, status displays, or any IOS panel.
---

# Workflow — read BEFORE doing anything

1. Read `~/simulator_framework/CLAUDE.md` — workflow rules first, then reference as needed
2. Read `~/simulator_framework/DECISIONS.md` — CURRENT STATE section
3. If a task card was provided, follow it literally — do not expand scope
4. Before ANY code change, state your plan (files, current behavior, new behavior, risks) and WAIT

When making structural decisions, append to DECISIONS.md CHANGE LOG:
```
## YYYY-MM-DD — hh:mm:ss - Claude Code
- DECIDED / REASON / AFFECTS
```

# Domain: IOS Backend + Frontend

## Package layout

| Component | Path | Technology |
|---|---|---|
| Backend | `src/ios_backend/` | Python: FastAPI + rclpy + WebSocket |
| Frontend | `ios/frontend/` | React + Zustand + Vite (NOT a ROS2 package) |

## Backend (`src/ios_backend/ios_backend_node.py`)

Run manually (never via launch file):
```bash
source /opt/ros/jazzy/setup.bash && source install/setup.bash
python3 -m uvicorn ios_backend.ios_backend_node:app --host 0.0.0.0 --port 8080
```

### Key behaviors
- Uses `rclpy.spin_once()` in a thread (NOT `rclpy.spin()` — fails under uvicorn)
- JSON encoder `_RosEncoder` handles numpy types from ROS2 arrays
- Dynamic node discovery via heartbeats + lifecycle + graph queries (3s interval, 5s startup delay)
- No hardcoded node list

### Topic subscriptions
`/sim/flight_model/state`, `/sim/state`, `/sim/fuel/state`, `/sim/navigation/state`,
`/sim/controls/avionics`, `/sim/electrical/state`, `/sim/alerts`, `/sim/engines/state`,
`/sim/diagnostics/heartbeat`, `/sim/diagnostics/lifecycle_state`, `/sim/failure_state`

### Topic publishers
- `/devices/instructor/panel` — IOS switch overrides (INSTRUCTOR priority, with switch_forced[])
- `/devices/instructor/controls/avionics` — IOS frequency tuning
- `/devices/virtual/panel` — cockpit page switches (VIRTUAL priority, no force flags)

**NEVER publish to `/sim/` topics from ios_backend.** Always `/devices/instructor/` or `/devices/virtual/`.

### WebSocket config messages (sent on connect + aircraft_id change)
- `avionics_config` — radios, displays from navigation.yaml
- `engine_config` — engine type, limits from engine.yaml
- `fuel_config` — tanks, display units from fuel.yaml
- `failures_config` — failure catalog from failures.yaml
- `electrical_config` — sources, buses, switches, loads from electrical.yaml

### WS command: set_panel
```json
{ "type": "set_panel", "data": {
    "switch_ids": ["sw_battery"], "switch_states": [true],
    "switch_forced": [true],
    "selector_ids": ["sel_magnetos"], "selector_values": [3],
    "selector_forced": [true]
}}
```
`switch_forced`/`selector_forced`: empty = normal command, `[true]` = force, `[false]` = release.

### REST endpoints
- `GET /api/navaids/search?q=&types=&limit=` — navaid search for failure panel

### IOS features implemented
- Sim state forwarding including `reposition_active` → frontend shows REPOSITIONING badge
- Failure catalog loaded from aircraft failures.yaml, grouped by ATA chapter
- Failure inject/clear/clear_all via WS commands
- Navaid station search UI for world navaid failures
- CMD_REPOSITION for IOS POS page departure/reposition
- Config-driven electrical panel from electrical.yaml (sources, buses, switches, loads, CBs)
- Per-switch FORCE checkbox on IOS A/C page — forces individual switches without locking others
- Aircraft YAML configs sent as WS messages: avionics_config, engine_config, fuel_config, failures_config, electrical_config

## Frontend (`ios/frontend/`)

Run: `cd ios/frontend && npm run dev` → http://localhost:5173

### URL routing
- `/` — main IOS app (map, status strip, 9 panel tabs, action bar)
- `/cockpit/c172/electrical` — virtual electrical panel (VIRTUAL priority)
- `/cockpit/c172/avionics` — placeholder

### Key files
- `src/store/useSimStore.js` — Zustand store, WebSocket handler, CMD dispatch
- `src/components/StatusStrip.jsx` — 3-row status (Row 3 = dynamic radio from navigation.yaml)
- `src/components/panels/AircraftPanel.jsx` — dynamic A/C page: radios, electrical, engine gauges, FORCE checkboxes
- `src/components/panels/PositionPanel.jsx` — airport search, runway selection, position icons
- `src/components/panels/FailuresPanel.jsx` — ATA-grouped catalog, navaid search, inject/clear
- `src/components/panels/NodesPanel.jsx` — dynamic node discovery, lifecycle controls
- `src/cockpit/CockpitElectrical.jsx` — virtual cockpit electrical panel (VIRTUAL priority, green styling)

### Design palette
bg `#0a0e17`, panel `#111827`, elevated `#1c2333`, borders `#1e293b`,
text `#e2e8f0`, dim `#64748b`, accent `#00ff88`, cyan `#39d0d8`, danger `#ff3b30`.
IOS switches: amber. Virtual cockpit: green.

### WebSocket wire keys → Zustand store keys
`com1_mhz` → `com1Mhz`, `nav1_mhz` → `nav1Mhz`, `adf1_khz` → `adf1Khz`

### IOS command rules
- IOS switches → `/devices/instructor/` (INSTRUCTOR priority)
- Virtual cockpit → `/devices/virtual/` (VIRTUAL priority)
- All UIs read state from `/sim/controls/panel` (arbitrated) — never own published commands

### Performance
- All components use `useShallow` selectors — only re-render when own data changes
- Backend throttles high-frequency WS topics (FDM, electrical, nav, etc.) to 10Hz max
- Float values rounded in backend callbacks to reduce JSON diff churn
- Track array capped at 10K points with progressive thinning
- Leaflet aircraft icon quantized to 2° heading steps

### Not yet implemented
- COM/NAV frequency entry UI
- Flight departure/arrival graphs
- Debrief panel
- SimSnapshot save/load

## Build / Run

Backend: `python3 -m uvicorn ios_backend.ios_backend_node:app --host 0.0.0.0 --port 8080`
Frontend: `cd ios/frontend && npm run dev`
Kill stale: `fuser -k 8080/tcp` (backend), `fuser -k 5173/tcp` (frontend)