---
name: messages
model: sonnet
description: >
  ROS2 message and service definitions in sim_msgs. Use when adding, modifying, or
  reviewing message types, topic naming, field conventions, or the message CMakeLists.
  Changes here ripple across all nodes — handle with care.
---

# Workflow — read BEFORE doing anything

1. Read `~/simulator_framework/CLAUDE.md` — workflow rules first, then reference as needed
2. Read `~/simulator_framework/DECISIONS.md` — CURRENT STATE section
3. Before ANY message change, state: which message, what fields change, which consumers break
4. WAIT for approval — message changes are breaking changes

When making structural decisions, append to DECISIONS.md CHANGE LOG:
```
## YYYY-MM-DD — hh:mm:ss - Claude Code
- DECIDED / REASON / AFFECTS
```

# Domain: sim_msgs — Message & Service Definitions

Package at `src/sim_msgs/`.

## Package structure

```
src/sim_msgs/
├── CMakeLists.txt     ← rosidl_generate_interfaces() with all .msg/.srv files
├── package.xml
├── msg/               ← all .msg definitions
└── srv/               ← all .srv definitions (SearchAirports, GetRunways, GetTerrainElevation)
```

## SimCommand constants (AUTHORITATIVE — verify against SimCommand.msg if in doubt)

```
CMD_RUN             = 0
CMD_FREEZE          = 1
CMD_UNFREEZE        = 2
CMD_RESET           = 3
CMD_LOAD_SCENARIO   = 4
CMD_SHUTDOWN        = 5
CMD_SET_IC          = 6
CMD_RELOAD_NODE     = 7
CMD_DEACTIVATE_NODE = 8
CMD_ACTIVATE_NODE   = 9
CMD_RESET_NODE      = 10
CMD_REPOSITION      = 11
```

**CRITICAL:** If these values look wrong, READ the actual SimCommand.msg file before
writing any code. Do not trust this document over the source file.

## FlightModelCapabilities constants

```
uint8 FDM_NATIVE = 0
uint8 EXTERNAL_COUPLED = 1
uint8 EXTERNAL_DECOUPLED = 2
```

## Field naming conventions (LOCKED)

### Avionics frequencies
- MHz: `_freq_mhz` suffix (e.g., `nav1_freq_mhz`)
- kHz: `_freq_khz` suffix (e.g., `adf1_freq_khz`)
- No bare `_freq` suffix

### Numbering
- `adf1`/`adf2`, `gps1`/`gps2`, `com1`/`com2`/`com3` — consistent suffix
- Never unprefixed

### NavigationState types
- `float64` for lat/lon (degrees) only
- `float32` for all other numerics
- `bool` for flags, `string` for idents

### Message naming
- State messages (periodic): `*State` (ElectricalState, FuelState)
- Command messages: `*Command` or `*Controls` (SimCommand, FlightControls)
- Capability messages: `*Capabilities`
- Injection messages: `*Injection` (FailureInjection)

## Three-tier avionics pipeline

```
RawAvionicsControls   /devices/*/controls/avionics
       ↓ input_arbitrator
AvionicsControls      /sim/controls/avionics
       ↓ navigation_node
NavigationState       /sim/navigation/state
```

Never skip a tier.

## Adding a new message

1. Create `msg/NewMessage.msg` in `src/sim_msgs/msg/`
2. Add to `rosidl_generate_interfaces()` in `CMakeLists.txt`
3. Full rebuild: `colcon build --symlink-install && source install/setup.bash`
4. **All running nodes must be restarted** — CDR mismatches cause garbled data

## Critical rules

- Changing a message is a breaking change — all consumers must rebuild
- Do NOT change failure messages (FailureState, FailureCommand, FailureInjection) without consulting the failures pipeline
- After any message change: full `colcon build` + restart all nodes
- Every new message must be in `CMakeLists.txt` rosidl list

## Build

Messages only: `colcon build --packages-select sim_msgs && source install/setup.bash`

After message changes, rebuild everything: `colcon build --symlink-install && source install/setup.bash`