# CIGI bridge — CCL-based rewrite (host + plugin)

**Date**: 2026-04-23 (revised same day to include host-side migration)
**Status**: design, awaiting user review + plan

## Problem

Today, CIGI wire handling in the framework is hand-rolled on **both**
sides:

- `src/core/cigi_bridge/src/cigi_host_node.cpp` — raw encoders for
  IG Control, Entity Control, HAT/HOT Request; raw parsers for SOF and
  HAT/HOT Extended Response. Plus `weather_encoder.cpp` for Atmosphere,
  Environmental Region, Weather Control, and the user-defined Runway
  Friction packet.
- `x-plane_plugins/xplanecigi/XPluginMain.cpp` — 1618 lines handling
  the IG-side direction. Every packet hand-parsed with `read_be*`,
  every response hand-built with `write_be*`.

Planned work over the next 6–12 months pushes both sides toward a
full CGF-style integration: ~20+ packet types in play, including AI
traffic entities with articulated gear/flaps, collision detection,
entity-bound weather regions, and HUD/MFD symbology. Each new packet
today costs two hand-rolled wire tables (host emit + plugin parse) and
one round of spec auditing — which the 2026-04-23 audit showed we
reliably get wrong at least once per packet.

Boeing's CCL 3.3.3 is already vendored in-repo at
`references/CIGI/cigi3.3/ccl/`. CCL's `Pack`/`Unpack` are the
canonical implementation of the spec — the same library this session's
round-trip test (`test_cigi_wire_conformance`) uses as an oracle.
Putting **both** sides of the bridge on CCL gives us a single
spec-conformance layer across the whole stack.

## Goal

Replace all hand-rolled CIGI wire handling — on the host node AND in
the X-Plane plugin — with a single CCL-backed library, structured as:

- **Typed emitters** for outbound (per-packet `BuildIgCtrl(...)`,
  `BuildEntityCtrl(...)`, etc.) that wrap CCL `Pack`.
- **Processor interfaces** for inbound, one per packet class. Users
  register processors with a session; the session dispatches incoming
  packets via CCL `Unpack`.

After the rewrite:

- Zero hand-counting of byte offsets anywhere in the framework.
- Adding a new packet type = add one emitter + one interface + one
  processor implementation. No wire-level work.
- The existing `test_cigi_wire_conformance` retires: with CCL on both
  sides, spec-conformance tests become CCL-vs-CCL tautologies.
  Correctness of our code depends on CCL being correct (it is) and
  us not regressing library usage (covered by smoke tests).

## Non-goals

- **CIGI 4.0 support**. Adoption is thin across commercial IGs;
  cost of maintaining dual 3.3/4.0 stacks outweighs benefits today.
  Revisit when a concrete driver appears. CCL 4.0 source is at
  `references/CIGI/ccl_4_0 rev6a/` — upgrade path stays open.
- **Linux `.so` build of the plugin**. Today's target is Windows via
  mingw64 cross-compile; that continues.
- **AI traffic entity spawning** via `XPLMInstance`. The plugin
  handles the ownship entity only in v1, same as today. Spawning
  AI entities is a meaningful sub-project for v2.

## Approach

### Architecture

One new library, used by two callers:

```
src/core/cigi_bridge/
├── cigi_session/                 ← new static library (host + IG)
│   ├── include/cigi_session/
│   │   ├── HostSession.h         ← wraps CigiHostSession + CigiOutgoingMsg
│   │   ├── IgSession.h           ← wraps CigiIGSession + CigiIncomingMsg
│   │   │
│   │   ├── emitters/             ← typed outbound builders
│   │   │   ├── IgCtrlEmit.h      ← BuildIgCtrl(frame, ts, ig_mode, db, last_ig_frame)
│   │   │   ├── EntityCtrlEmit.h  ← BuildEntityCtrl(id, state, pose, ...)
│   │   │   ├── HatHotReqEmit.h   ← BuildHatHotRequest(req_id, lat, lon)
│   │   │   ├── AtmosphereEmit.h
│   │   │   ├── EnvRegionEmit.h
│   │   │   ├── WeatherCtrlEmit.h
│   │   │   ├── CompCtrlEmit.h    ← runway friction lives here (Option 2)
│   │   │   ├── ArtPartEmit.h     ← (future, v2)
│   │   │   ├── RateCtrlEmit.h    ← (future, v2)
│   │   │   └── SofEmit.h         ← IG side
│   │   │
│   │   ├── processors/           ← inbound processor interfaces
│   │   │   ├── IIgCtrlProcessor.h
│   │   │   ├── IEntityCtrlProcessor.h
│   │   │   ├── IHatHotReqProcessor.h
│   │   │   ├── ISofProcessor.h           ← host side consumes SOF
│   │   │   ├── IHatHotRespProcessor.h    ← host side consumes response
│   │   │   ├── IAtmosphereProcessor.h
│   │   │   ├── IEnvRegionProcessor.h
│   │   │   ├── IWeatherCtrlProcessor.h
│   │   │   ├── ICompCtrlProcessor.h
│   │   │   ├── IArtPartProcessor.h       ← (future, v2)
│   │   │   └── IRateCtrlProcessor.h      ← (future, v2)
│   │   │
│   │   └── Defaults.h            ← no-op default processors
│   └── src/                      ← implementations (~600 lines total)
│
├── src/                          ← host node, shrinks
│   ├── cigi_host_node.cpp        ← no more raw encoders — uses cigi_session
│   ├── hat_request_tracker.cpp   ← unchanged
│   ├── weather_sync.cpp          ← unchanged (logic), calls switch to emitters
│   └── weather_encoder.cpp       ← DELETED (replaced by emitters)
│
└── (vendored cigi_ig_interface/ — unchanged, still reference-only)

x-plane_plugins/xplanecigi/       ← shrinks from 1618 → ~400 lines
├── XPluginMain.cpp               ← plugin lifecycle, socket I/O, IgSession ownership
├── XplaneIgCtrlProcessor.cpp     ← IG Mode transitions + terrain probe triggers
├── XplaneEntityProcessor.cpp     ← writes X-Plane ownship datarefs
├── XplaneHatHotProcessor.cpp     ← XPLMProbeTerrainXYZ + response emit
├── XplaneWeatherProcessor.cpp    ← atmosphere + env region + weather ctrl
└── XplaneCompCtrlProcessor.cpp   ← runway friction (and future global terrain items)
```

### Data flow

**Host → IG (host emits, plugin processes)**

```
sim_state → cigi_host_node → emitters → HostSession::Serialize()
→ UDP datagram → plugin recvfrom → IgSession::HandleDatagram(bytes, len)
→ CCL Unpack → processor dispatch → XplaneFooProcessor::OnFoo(...)
→ XPLM* dataref writes
```

**IG → Host (plugin emits, host processes)**

```
XPLM probe result → XplaneHatHotProcessor → emitter → IgSession out
→ UDP → host recv → HostSession::HandleDatagram → processor dispatch
→ publisher → /sim/cigi/hat_responses
```

Library never touches X-Plane, never touches ROS2. Keeps the protocol
stuff isolated from platform code — the same principle that makes the
existing `hat_request_tracker` and `weather_sync` clean.

### Runway Friction over Component Control (not user-defined 0xCB)

Replace the user-defined `0xCB` packet with standard Component
Control (§4.1.4):

```
Component Class  = 8      (Global Terrain Surface)
Component ID     = 100    (framework convention: "Runway Friction")
Component State  = friction_level (0..15)
```

Rationale captured in the previous spec revision — the shortest
version: Component Control is the CIGI-native mechanism for exactly
this case; spec Table 9 gives runway lights / cultural lights /
shadows as examples of Global Terrain Surface components; commercial
IGs all parse Component Control by default.

Framework reserves Component IDs in a small table documented in the
cigi_session library README:

| ID  | Class                  | Meaning                      |
|-----|------------------------|------------------------------|
| 100 | Global Terrain Surface | Runway Friction (0..15 enum) |
| 101 | reserved               |                              |
| 102 | reserved               |                              |
| ... |                        |                              |

After this change, the framework on-wire inventory is 100%
spec-conformant CIGI 3.3 — no user-defined packets at all.

### Build

**Two build targets for CCL and for `cigi_session`**:

- **Linux native** (for host + library tests). Already building.
- **Windows mingw64** (for plugin). Need to verify CCL builds cleanly
  with `x86_64-w64-mingw32-g++`. If not, static-source fallback:
  list CCL's ~50 `.cpp` files directly into the plugin's CMakeLists
  target — bypasses CCL's own build machinery.

`cigi_session` is part of `src/core/cigi_bridge/` (per user decision —
keep it in the cigi_bridge package, don't promote to its own ROS2
package). Its CMake builds twice:

1. As part of the ROS2 package (links into `cigi_bridge_node` on the
   host side, native Linux toolchain).
2. As a second target using the mingw64 toolchain (links into
   `xplanecigi.xpl` on the plugin side, cross-compile).

Plugin's `CMakeLists.txt` pulls in `cigi_session` via
`add_subdirectory(../../src/core/cigi_bridge/cigi_session build-ccl-mingw)`
or by importing a prebuilt static lib — choice made at implementation
time based on which integrates cleanest with the mingw64 flow.

### Testing

**End state**: no low-level wire-format test. With CCL on both sides,
`our emitter(x)` → `wire bytes` → `our processor receives x` is a
CCL-wrapping-CCL tautology. The existing
`test_cigi_wire_conformance.cpp` is **deleted** as part of the
migration.

**What stays**:

- `test_weather_encoder` and `test_weather_sync` — these cover the
  host's weather-state-diff logic, not wire format. They adapt to
  call emitters instead of `weather_encoder.cpp` functions; otherwise
  unchanged.
- **Smoke test** — host + plugin + X-Plane over localhost, manual.
  Verify reposition, HAT/HOT, weather, runway friction each work
  end-to-end after the migration.

**During migration**: `test_cigi_wire_conformance` stays green as a
safety net. Every encoder migration step is: swap implementation
from raw → CCL emitter, rebuild, run test. When test still passes,
CCL is producing the same bytes the raw encoder did. When the final
packet has migrated, delete the test.

### Migration sequence

**Phase 0 — prerequisites**
1. Prove `cigi_session` + CCL cross-compile under mingw64. If
   CCL's CMake rejects the toolchain, add static-source fallback.
   Blocker for plugin-side migration.

**Phase 1 — library scaffold (no caller changes)**
2. Scaffold `cigi_session/` with `HostSession`, `IgSession`, one
   emitter + one processor interface (IG Control). Runs the simplest
   round-trip: emit → send to self via in-memory buffer → unpack.
   Establishes the API patterns.

**Phase 2 — host-side migration (host-only, plugin unchanged)**

Per-packet, each a separate commit. Each commit:

   a. Add emitter/processor in library.
   b. Replace `cigi_host_node.cpp` hand-rolled function with emitter
      call (or hand-rolled parser with processor registration).
   c. Rebuild. `test_cigi_wire_conformance` confirms wire bytes
      unchanged.

Order (smallest → largest):

3. IG Control emitter → replace `encode_ig_ctrl`.
4. Entity Control emitter → replace `encode_entity_ctrl`.
5. HAT/HOT Request emitter → replace `encode_hot_request`.
6. SOF processor → replace SOF parse branch in `recv_pending`.
7. HAT/HOT Extended Response processor → replace response parse branch.
8. Atmosphere Control emitter → replace
   `weather_encoder::encode_atmosphere_control`.
9. Weather Control emitter (global + regional) → replace
   `weather_encoder::encode_weather_control`, `encode_weather_control_regional`.
10. Environmental Region Control emitter → replace
    `weather_encoder::encode_region_control`.
11. Component Control emitter + host-side switch from `0xCB` to
    Component Control (Global Terrain Surface / ID=100) → delete
    `encode_runway_friction`.
12. Delete `weather_encoder.cpp` entirely. Adjust `weather_sync.cpp`
    to call emitters directly.
13. Delete `CIGI_PKT_*` constants from `cigi_host_node.hpp`.

**Phase 3 — plugin-side migration (plugin-only, host stable on CCL)**

Same per-packet pattern. Each commit replaces one `case 0x…:` in
`XPluginMain.cpp` with a library processor registration.

14. IgSession bring-up + SOF emitter (replaces `send_sof`).
15. `XplaneIgCtrlProcessor`.
16. `XplaneEntityProcessor` (ownship-only).
17. `XplaneHatHotProcessor` (+ HAT/HOT Ext Response emitter).
18. `XplaneWeatherProcessor` (atmosphere + env region + weather ctrl).
19. `XplaneCompCtrlProcessor` (+ plugin-side handling of runway
    friction via Component Class 8 / ID 100). Host half landed in
    step 11.

**Phase 4 — cleanup**
20. Delete `test_cigi_wire_conformance.cpp` — CCL on both sides makes
    it tautological.
21. Delete residual `read_be*` / `write_be*` helpers in
    `XPluginMain.cpp`.
22. Delete vendored `cigi_ig_interface/` static-lib target? (Open
    question — only if nothing else references it. Hand it off to
    v2.)
23. End-to-end smoke test: reposition, HAT/HOT, weather, runway
    friction all working on localhost.
24. Update `DECISIONS.md` with the architectural switch + rationale.

**Total effort**: ~3–5 days focused. Phase 0 + 1 is the risky start
(mingw64 unknown); Phase 2 is mechanical; Phase 3 is parallelizable
with Phase 2 once the library is stable.

## Risks

- **mingw64 + CCL build**. First task, blocker if it fails. Fallback
  is static-source inclusion. Estimate: half a day if CCL CMake
  supports mingw; up to a full day for static-source fallback.
- **CCL's `Unpack` Spec-parameter requirements** scattered across
  packet classes (Entity Control wants `CigiAnimationTable`, SOF
  wants `CigiVersionID`, others TBD). Discovered per-packet during
  migration. Cost: minor, well-bounded.
- **Host-side regression during Phase 2**. Smoke-test after each
  packet; `test_cigi_wire_conformance` is the safety net until it's
  retired in Phase 4.
- **Plugin rebuild cadence during Phase 3** — X-Plane needs the new
  `.xpl` deployed via `copyxplaneplugin.sh` after every change; if
  running a live smoke test, expect one X-Plane restart per packet
  migrated. Annoying, not risky.

## Files changed

**Created**
- `src/core/cigi_bridge/cigi_session/` — new library subdirectory
  (headers + sources + its own CMake).
- `x-plane_plugins/xplanecigi/Xplane*Processor.cpp` — six new files,
  ~600 lines total.

**Modified**
- `src/core/cigi_bridge/CMakeLists.txt` — add `cigi_session` target;
  adjust `cigi_bridge_node` link.
- `src/core/cigi_bridge/src/cigi_host_node.cpp` — drop hand-rolled
  encoders/parsers; call library API.
- `src/core/cigi_bridge/include/cigi_bridge/cigi_host_node.hpp` —
  drop `CIGI_PKT_*` constants and other wire-level helpers.
- `src/core/cigi_bridge/src/weather_sync.cpp` — calls switch from
  `weather_encoder::encode_*` to `cigi_session::BuildFoo`.
- `src/core/cigi_bridge/test/test_weather_encoder.cpp` +
  `test_weather_sync.cpp` — adapt call sites, same coverage.
- `x-plane_plugins/xplanecigi/XPluginMain.cpp` — 1618 → ~400 lines,
  all packet parsing replaced by `IgSession::HandleDatagram`.
- `x-plane_plugins/xplanecigi/CMakeLists.txt` — cross-build CCL +
  `cigi_session`, link both.
- `DECISIONS.md` — record the architecture and the three trailing
  deletions (0xCB, weather_encoder.cpp, test_cigi_wire_conformance).

**Deleted**
- `src/core/cigi_bridge/src/weather_encoder.cpp` (absorbed into
  emitters).
- `src/core/cigi_bridge/test/test_cigi_wire_conformance.cpp` (CCL
  on both sides makes it tautological).
- `CIGI_PKT_RUNWAY_FRICTION` constant + associated 0xCB handling in
  both host and plugin.
- `XPluginMain.cpp`'s entire `switch (packet_id) { … }` block and all
  `read_be*` / `write_be*` helpers.

## Open questions

None. All decisions made:

- Library path: `src/core/cigi_bridge/cigi_session/`, inside the
  cigi_bridge package (not its own ROS2 package).
- Test retirement: delete `test_cigi_wire_conformance` post-migration.
- Host migration: in scope (this design revision).
- Runway friction: Component Control, Class 8, ID 100.
- CIGI 4.0: deferred.
- AI traffic entities: deferred to v2.

## Links

- Host-side spec-conformance work (foundation): branch
  `cigi-spec-conformance`, commits `2afb60f`, `ce7e52d`, `70985f4`,
  `26b3999`, `7363076`, `f9edaf0`, `7982b93`, `726e8b8`, `453ccbb`,
  `9fb73eb`, `1ef0dee`, `502df9c`.
- Audit report: `~/.claude/plans/cigi_3_3_audit_report.md`.
- CCL source: `references/CIGI/cigi3.3/ccl/` (from
  `cigi_ccl_src_ver_3_3_3.zip`, extracted + built 2026-04-23).
- CIGI 3.3 packet catalog:
  `.claude/skills/cigi_spec/references/cigi 3.3 packets.md`
  (verified against CCL + Wireshark 2026-04-23).
