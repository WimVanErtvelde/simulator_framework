---
name: aircraft
model: sonnet
description: >
  Aircraft configuration packages: config YAMLs, pluginlib plugins, flight model data.
  Use when adding or modifying an aircraft type — config files, system plugins,
  plugin registration, or flight model adapter settings.
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

# Domain: Aircraft Configuration Packages

You are working on aircraft-specific configuration and plugins under `src/aircraft/`.

## Package structure per aircraft

```
src/aircraft/<type>/
├── CMakeLists.txt
├── package.xml
├── plugins.xml             ← pluginlib class registration
├── config/
│   ├── config.yaml         ← metadata, required nodes, FDM type, limits, default IC, gear_points
│   ├── electrical.yaml     ← bus topology, sources, loads, switch IDs
│   ├── fuel.yaml           ← tanks, selectors, pumps, density (cockpit interface — physics owned by FDM)
│   ├── engine.yaml         ← engine type, count, panel control IDs
│   ├── gear.yaml           ← gear type, retractable flag, leg names
│   ├── air_data.yaml       ← pitot-static systems, heat load names, alternate static switch IDs
│   ├── failures.yaml       ← failure catalog (ATA chapter grouped)
│   └── navigation.yaml     ← installed avionics (drives dynamic IOS A/C page)
└── src/
    ├── electrical_model.cpp
    ├── fuel_model.cpp
    ├── engines_model.cpp
    ├── gear_model.cpp
    └── air_data_model.cpp
```

## Current aircraft

| Aircraft | Path | FDM | Plugins |
|---|---|---|---|
| C172 | `src/aircraft/c172/` | JSBSim (c172p) | Electrical, Fuel, Engines, Gear, AirData |
| EC135 | `src/aircraft/ec135/` | Helisim 6.0 (future) | Electrical, Engines (fuel stub) |

## Config conventions

### config.yaml
```yaml
aircraft:
  id: c172
  name: "Cessna 172S Skyhawk"
  type: fixed_wing          # or rotary_wing
  fdm: jsbsim
  required_nodes:
    - sim_electrical
    - sim_fuel
    - sim_engine_systems
    - sim_navigation
    - sim_gear
    - sim_air_data
  gear_points:              # body-frame offsets for CIGI HOT
    nose:       { x_m: 1.3, y_m: 0.0, z_m: -1.8 }
    left_main:  { x_m: -0.4, y_m: -1.2, z_m: -1.8 }
    right_main: { x_m: -0.4, y_m: 1.2, z_m: -1.8 }
  default_ic:
    configuration: ready_for_takeoff
    latitude_rad: 0.8885
    longitude_rad: 0.0783
    altitude_msl_m: 58.0
    heading_rad: 4.432
    airspeed_ms: 0.0
```

### Panel control ID naming
- `sw_` — boolean switch
- `btn_` — momentary button
- `cb_` — circuit breaker
- `sel_` — detented rotary selector
- `pot_` — analog potentiometer
- `enc_abs_` / `enc_rel_` — encoder

### Plugin naming
`aircraft_<id>::<Model>` — e.g., `aircraft_c172::C172ElectricalModel`

### fuel.yaml ownership split
- **Physics** (position, capacity, unusable, drain) — owned by FDM (JSBSim XML)
- **Cockpit interface** (tank names, selectors, pump IDs, display_unit, density) — owned by fuel.yaml
- Never duplicate FDM physics config in our YAML

## Key principle

**Never hardcode aircraft-specific values in framework node code.** If you're writing
`if (aircraft == "c172")` in a systems node, it's an architectural mistake.
Use config fields that the aircraft YAML provides.

## Build

`colcon build --packages-select aircraft_c172 aircraft_ec135 && source install/setup.bash`

Full rebuild: `colcon build --symlink-install && source install/setup.bash`