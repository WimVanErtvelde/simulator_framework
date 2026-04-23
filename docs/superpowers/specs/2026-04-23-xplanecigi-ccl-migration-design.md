# xplanecigi — CCL-based rewrite

**Date**: 2026-04-23
**Status**: design, awaiting user review + plan

## Problem

The X-Plane CIGI plugin (`x-plane_plugins/xplanecigi/XPluginMain.cpp`,
1618 lines) hand-rolls every CIGI 3.3 packet encode and decode. It
handles 7 standard packets plus one framework-invented user-defined
packet today (IG Control, Entity Control, HAT/HOT Request, Atmosphere,
Env Region, Weather Control, Runway Friction; SOF and HAT/HOT Extended
Response on send).

Planned work over the next 6–12 months pushes the plugin toward a full
CGF-style integration: ~20+ packet types total, including AI traffic
entities with articulated gear/flaps, collision detection, entity-bound
weather regions, and HUD/MFD symbology. Hand-rolling another dozen
byte-offset decoders is both high-effort and high-risk — every one is a
re-run of the bugs the host-side audit just uncovered (wrong offsets,
wrong field widths, wrong enum values).

Boeing's CIGI Class Library (CCL, 3.3.3) is already vendored under
`references/CIGI/cigi3.3/ccl/`. CCL's `Pack`/`Unpack` are the canonical
implementation of the spec — the same library the host-side round-trip
test (`test_cigi_wire_conformance`) now uses as its oracle. Moving the
plugin onto CCL brings it under the same spec-conformance umbrella and
eliminates hand-written offset work for every future packet.

## Goal

Replace the plugin's hand-rolled CIGI layer with a thin library over
CCL, structured as processor interfaces the plugin implements with
X-Plane-specific behaviour. After the rewrite:

- Every byte offset in the plugin's wire layer comes from CCL, not from
  hand-counting.
- Adding a new packet type becomes: register a processor interface in
  the library, implement it in the plugin, wire the handler. No offset
  or bit-field code.
- The plugin's `.cpp` files contain only X-Plane integration
  (XPLMProbeTerrain, XPLMWeather datarefs, plugin lifecycle).

## Non-goals

- **Host-side CCL migration**. The host
  (`src/core/cigi_bridge/src/cigi_host_node.cpp`) keeps its raw
  encoders. It has its own CCL round-trip test guarding spec
  conformance. Migrating the host is a separate future project.
- **CIGI 4.0 support**. Adoption is thin across commercial IGs; cost of
  maintaining dual 3.3/4.0 stacks outweighs benefits today. Revisit
  when a concrete driver (customer, IG integration) appears. CCL 4.0
  source is already in `references/CIGI/ccl_4_0 rev6a/` so the upgrade
  path stays open.
- **Linux `.so` build of the plugin**. Today's target is Windows via
  mingw64 cross-compile; that continues.

## Approach

### Architecture

Two artifacts:

**`src/core/cigi_bridge/cigi_ig_session/`** — new static library.
Handles protocol: CCL session setup, packet-ID dispatch, processor
registry. No X-Plane dependencies. Can be unit-tested on Linux host
(pairs cleanly with the existing host round-trip test).

**`x-plane_plugins/xplanecigi/`** — plugin shrinks from 1618 lines to
~600. Keeps plugin lifecycle, socket I/O, SOF emit, and one
X-Plane-specific processor implementation per CIGI packet class it
handles. No byte-offset code.

### Component breakdown

**Library (`cigi_ig_session`)**

- `IgSession` — owns a `CigiIncomingMsg` + `CigiOutgoingMsg`, a processor
  registry keyed by packet class, and the `HandleDatagram(bytes, len)`
  entry point. Walks CCL's parsed packet stream and dispatches each to
  the registered processor.
- Processor interfaces (one per CIGI packet the plugin handles now or
  will handle soon):
    - `IIgCtrlProcessor`    — IG Control (§4.1.1)
    - `IEntityCtrlProcessor` — Entity Control (§4.1.2)
    - `IArtPartProcessor`    — Articulated Part Control (§4.1.6)
    - `IShortArtPartProcessor`— Short Articulated Part Control (§4.1.7)
    - `IRateCtrlProcessor`   — Rate Control (§4.1.8)
    - `IAtmosphereProcessor` — Atmosphere Control (§4.1.10)
    - `IEnvRegionProcessor`  — Environmental Region Control (§4.1.11)
    - `IWeatherCtrlProcessor`— Weather Control (§4.1.12)
    - `IHatHotProcessor`     — HAT/HOT Request (§4.1.24)
    - `ICompCtrlProcessor`   — Component Control (§4.1.4), used by
      runway friction (see §Runway Friction below) + future symbology
      bits.
- Default no-op processor implementations so the plugin can register
  only what it handles; unknown packets log-and-drop rather than crash.
- `SofBuilder` — thin wrapper around CCL's `CigiSOFV3_2::Pack` with the
  fields the plugin sends (IG Mode, frame numbers, DB number).

**Plugin (`x-plane_plugins/xplanecigi/`)**

- `XPluginMain.cpp` — plugin lifecycle (`XPluginStart`,
  `XPluginReceiveMessage`, etc.), UDP socket management, flight loop
  callback, SOF emission, and processor ownership. Creates one
  `IgSession` + registers its processors.
- `XplaneIgCtrlProcessor.cpp` — parses IG Mode transitions, triggers
  terrain-probe stability check on Standby→Operate.
- `XplaneEntityProcessor.cpp` — **Ownship only for v1**: sets
  `sim/flightmodel/position/{local_x,y,z}` + `{phi,theta,psi}` datarefs
  as today. Future v2: spawn AI aircraft via `XPLMInstance` for
  non-ownship Entity IDs.
- `XplaneHatHotProcessor.cpp` — `XPLMProbeTerrainXYZ` →
  build-response-via-library → send.
- `XplaneWeatherProcessor.cpp` — implements `IAtmosphereProcessor`,
  `IEnvRegionProcessor`, `IWeatherCtrlProcessor`. Writes
  `XPLMSetWeatherAtLocation` + global weather datarefs.
- `XplaneCompCtrlProcessor.cpp` — routes incoming Component Control
  packets by (Class, Component ID). For v1 it handles exactly one
  component: `Global Terrain Surface / ID=100 / Runway Friction` →
  global runway friction dataref. Future: runway lights, shadows,
  cultural light groups.

### Runway Friction

**Change**: replace the user-defined `0xCB` packet with a standard
**Component Control** (0x04) on both host and plugin.

- Host side: `weather_encoder.cpp` emits one Component Control per
  runway-friction update:
  ```
  Component Class = 8 (Global Terrain Surface)
  Component ID    = 100  (framework convention: "Runway Friction")
  Component State = friction_level (0..15)
  ```
- Plugin side: `XplaneCompCtrlProcessor::OnComponentControl(...)`
  dispatches on `(Component Class, Component ID)` and sets the runway
  friction dataref.

Rationale: Component Control is the CIGI-native mechanism for exactly
this case (spec §4.1.4 Table 9 gives "Runway Lights", "Cultural
Lights", "Shadows" as Global Terrain Surface component examples).
Every CCL-based IG handles Component Control by default; any future
commercial IG integration either matches Component ID 100 or gets a
small mapping table. Removing `0xCB` means the framework's on-wire
packet inventory is 100% spec-conformant CIGI 3.3 — no user-defined
packets at all.

The `0xCB` constant, encoder in `weather_encoder.cpp`, and parser in
`XPluginMain.cpp` are deleted.

### Build

**CCL cross-compile to Windows via mingw64 is the biggest unknown.**
CCL's CMake documentation mentions Visual Studio support, not mingw.
First implementation step proves it builds; if it doesn't, fallback
is static-source (`add_library(cigicl STATIC ${CCL_SOURCES})` listing
the ~50 CIGI 3.x .cpp files from `references/CIGI/cigi3.3/ccl/source/`
directly in the plugin's CMakeLists, bypassing CCL's own build
machinery).

Two target builds coexist:

- `references/CIGI/cigi3.3/ccl/build/` — Linux native. Already exists;
  used by `test_cigi_wire_conformance` on the host side.
- `references/CIGI/cigi3.3/ccl/build-mingw/` — Windows cross. New.
  Built explicitly via `cmake -B build-mingw -DCMAKE_TOOLCHAIN_FILE=...`
  before the plugin build.

`x-plane_plugins/xplanecigi/CMakeLists.txt` adds:

- `find_package(cigi_ig_session REQUIRED)` (new library)
- `target_link_libraries(xplanecigi PRIVATE cigi_ig_session cigicl)`

### Testing

- **Library-side wire round-trip test** — `test_cigi_ig_session_conformance`,
  lives next to the host-side test under
  `src/core/cigi_bridge/test/`. Tests the library (`cigi_ig_session`)
  in isolation — no X-Plane, no XPLM headers, no plugin linkage.
  For each packet class the library dispatches:
    - Synthesise wire bytes with CCL `Pack`
    - Feed to the library via `IgSession::HandleDatagram`
    - Register a **spy processor** (test double implementing the
      processor interface with a `std::optional<CapturedFields>`)
    - Assert the captured fields match what CCL decoded
  - For outbound: library's `SofBuilder` emits bytes → CCL `Unpack` →
    assert fields round-trip.
  - Coverage scope: everything the library does. The X-Plane-specific
    processor implementations (the `XplaneFooProcessor.cpp` files) are
    NOT covered here — they call into XPLM and can only be verified by
    running against X-Plane.
- **Host-side test** (existing `test_cigi_wire_conformance`) stays.
- **End-to-end smoke test** — host + plugin over localhost UDP, as
  today. No scripted test, manual.
- **Existing host-side `test_cigi_wire_conformance`** — unchanged, still
  gates host spec conformance.

Both wire tests share the same CCL oracle, so host + plugin can't drift
from spec without one of the tests failing.

### Migration sequence

1. **Prove mingw64+CCL builds.** If CCL's CMake doesn't support the
   toolchain, add the static-source fallback. Blocker for everything
   else.
2. **Scaffold `cigi_ig_session`** — `IgSession` + processor interfaces
   + no-op defaults + `SofBuilder`. Linux build, no plugin changes
   yet.
3. **Plugin wire test scaffolding** — `test_xplanecigi_wire_conformance`
   target in CMakeLists. Fails (no processors implemented yet).
4. **Port handlers in dependency order**, one packet per commit:
   1. `IIgCtrlProcessor` + Xplane impl (smallest, no dependencies)
   2. SOF emit via `SofBuilder` (replaces hand-rolled `send_sof`)
   3. `IHatHotProcessor` + Xplane impl + extended response on send
   4. `IEntityCtrlProcessor` (ownship-only) + Xplane impl
   5. `IAtmosphereProcessor` + Xplane impl
   6. `IEnvRegionProcessor` + Xplane impl
   7. `IWeatherCtrlProcessor` + Xplane impl
   8. `ICompCtrlProcessor` + Xplane impl → **and** host switches from
      `0xCB` to Component Control in the same commit
5. **Delete dead code** in `XPluginMain.cpp`: `read_be*`, `write_be*`,
   the entire `switch(packet_id)` block. Replace with
   `session.HandleDatagram(pkt, len)`.
6. **Delete `0xCB` encoder** in `weather_encoder.cpp` + the constant.
   Update `weather_sync.cpp` if it references the old encoder.
7. **End-to-end smoke test** — reposition works, HAT/HOT flows, weather
   updates visible, runway friction propagates.
8. **Update DECISIONS.md** with the migration + new architecture.

## Risks

- **mingw64+CCL builds don't work out of the box**. Mitigation:
  static-source fallback (listed above). Costs one extra day of work
  if triggered.
- **CCL's `Unpack` Spec parameter requirements are scattered** — Entity
  Control needs `CigiAnimationTable`, SOF needs `CigiVersionID`, others
  might need things we haven't seen. Discovered during port; cost is
  reading CCL source for each packet.
- **Entity Control ownship vs AI split** — v1 handles ownship only,
  same as today. AI entity spawning via `XPLMInstance` is complex
  enough to deserve its own design sub-project. Not in scope here.
- **Runway Friction over Component Control loses atomicity of
  0xCB's one-uint8 payload** — Component Control is 32 bytes vs 8.
  Trivial bandwidth cost (one packet per weather update, <1 Hz).

## Files changed

**Created**:
- `src/core/cigi_bridge/cigi_ig_session/` — new library (headers +
  sources, ~400 lines).
- `x-plane_plugins/xplanecigi/Xplane*Processor.cpp` — six new files
  (~600 lines total).
- `src/core/cigi_bridge/test/test_cigi_ig_session_conformance.cpp` —
  new test (~300 lines, library-only, no X-Plane).
- `references/CIGI/cigi3.3/ccl/build-mingw/` — cross-build artifact
  dir (build output, not checked in).

**Modified**:
- `x-plane_plugins/xplanecigi/XPluginMain.cpp` — shrinks ~1600 → ~400
  lines. All packet parsing deleted; lifecycle/socket/SOF-emit/dispatch
  retained.
- `x-plane_plugins/xplanecigi/CMakeLists.txt` — link against
  `cigi_ig_session` + `cigicl`.
- `src/core/cigi_bridge/src/weather_encoder.cpp` — replace `0xCB`
  encoder with Component Control emit for runway friction.
- `src/core/cigi_bridge/CMakeLists.txt` — add `cigi_ig_session`
  target.
- `DECISIONS.md` — record the architecture switch and rationale.

**Deleted**:
- `CIGI_PKT_RUNWAY_FRICTION` / `CIGI_RUNWAY_FRICTION_SIZE` constants +
  `case 0xCB:` handler in the plugin.

## Open questions

None. All major decisions made:

- Library path: thin CCL wrapper with processor interfaces, fresh
  (not built on `~/CIGI_IG_Interface`).
- CCL source: `references/CIGI/cigi3.3/ccl/` (canonical Boeing/SISO
  vendored in-repo).
- Runway friction: Component Control (0x04), Global Terrain Surface
  class, framework-defined Component ID = 100.
- CIGI 4.0: not now.
- Host migration to CCL: not now.

## Links

- Host-side conformance work (foundation for this): branch
  `cigi-spec-conformance`, commits `2afb60f`, `ce7e52d`, `70985f4`,
  `26b3999`, `7363076`, `f9edaf0`.
- Audit report: `~/.claude/plans/cigi_3_3_audit_report.md`.
- CCL source: `references/CIGI/cigi3.3/ccl/` (from
  `cigi_ccl_src_ver_3_3_3.zip`, extracted + built 2026-04-23).
- CIGI 3.3 spec: `.claude/skills/cigi_spec/references/cigi 3.3 packets.md`
  (catalog, verified against CCL + Wireshark this session).
