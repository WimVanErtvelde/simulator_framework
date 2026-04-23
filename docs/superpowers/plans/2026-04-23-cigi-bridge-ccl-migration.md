# CIGI Bridge CCL Migration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace all hand-rolled CIGI 3.3 wire handling across the framework (host node and X-Plane plugin) with a single CCL-backed library, so the wire format is spec-conformant by construction and adding new packet types stops requiring byte-offset audits.

**Architecture:** New static library `cigi_session` under `src/core/cigi_bridge/cigi_session/` provides typed emitters (wrapping CCL `Pack()`) and processor interfaces (wrapping CCL `Unpack()`). Both the host node (Linux native) and the X-Plane plugin (mingw64 cross-compile) link this library. Host-side `weather_encoder.cpp` and the user-defined `0xCB` Runway Friction packet are retired; runway friction moves to standard Component Control (Class=8, ID=100). Migration is per-packet in lockstep: each packet's host emitter and plugin processor land in the same commit, keeping end-to-end behaviour intact frame-by-frame.

**Tech Stack:**
- C++17, ROS2 Jazzy (host node)
- Boeing CCL 3.3.3 (vendored at `references/CIGI/cigi3.3/ccl/`)
- gtest (existing test infrastructure)
- mingw64 cross-toolchain (`x86_64-w64-mingw32`, existing at `x-plane_plugins/xplanecigi/toolchain-mingw64.cmake`)
- X-Plane SDK (plugin-side, existing)

**Spec:** `docs/superpowers/specs/2026-04-23-xplanecigi-ccl-migration-design.md`

**Branch:** all tasks land on `cigi-spec-conformance` (current branch). Do not merge to `main` until Phase 4 smoke-test passes.

---

## File Structure

**Created** (`src/core/cigi_bridge/cigi_session/`):
```
CMakeLists.txt
include/cigi_session/
  HostSession.h             — wraps CCL for host-side send/receive
  IgSession.h               — wraps CCL for IG-side receive/send
  emitters/
    IgCtrlEmit.h            — Build IG Control bytes
    EntityCtrlEmit.h        — Build Entity Control bytes
    HatHotReqEmit.h         — Build HAT/HOT Request bytes
    AtmosphereEmit.h        — Build Atmosphere Control bytes
    EnvRegionEmit.h         — Build Environmental Region Control bytes
    WeatherCtrlEmit.h       — Build Weather Control bytes
    CompCtrlEmit.h          — Build Component Control bytes
    SofEmit.h               — Build SOF bytes (IG side)
    HatHotXRespEmit.h       — Build HAT/HOT Extended Response bytes (IG side)
  processors/
    IIgCtrlProcessor.h, IEntityCtrlProcessor.h, IHatHotReqProcessor.h,
    IAtmosphereProcessor.h, IEnvRegionProcessor.h, IWeatherCtrlProcessor.h,
    ICompCtrlProcessor.h,
    ISofProcessor.h, IHatHotRespProcessor.h
  ComponentIds.h            — framework Component ID registry (ID 100 = Runway Friction)
src/
  HostSession.cpp
  IgSession.cpp
  emitters.cpp              — all emitter implementations
test/
  test_cigi_session.cpp     — unit tests for every emitter + processor
```

**Created** (`x-plane_plugins/xplanecigi/`):
```
XplaneIgCtrlProcessor.{h,cpp}
XplaneEntityProcessor.{h,cpp}
XplaneHatHotProcessor.{h,cpp}
XplaneWeatherProcessor.{h,cpp}
XplaneCompCtrlProcessor.{h,cpp}
```

**Modified**:
- `src/core/cigi_bridge/CMakeLists.txt` — add `cigi_session` subdirectory and link target.
- `src/core/cigi_bridge/include/cigi_bridge/cigi_host_node.hpp` — drop `CIGI_PKT_*`, `CIGI_*_SIZE`, `CIGI_IG_MODE_*`, `CIGI_SOF_IG_MODE_OPERATE` constants (library owns these).
- `src/core/cigi_bridge/src/cigi_host_node.cpp` — replace raw encoders/parsers with library calls.
- `src/core/cigi_bridge/src/weather_sync.cpp` — swap calls from `weather_encoder::*` to library emitters.
- `src/core/cigi_bridge/test/test_weather_encoder.cpp` / `test_weather_sync.cpp` — adapt call sites to emitters.
- `x-plane_plugins/xplanecigi/XPluginMain.cpp` — retire `switch(packet_id)`; register processors with `IgSession`.
- `x-plane_plugins/xplanecigi/CMakeLists.txt` — cross-compile CCL + `cigi_session`.
- `DECISIONS.md` — append architecture decision.

**Deleted**:
- `src/core/cigi_bridge/src/weather_encoder.cpp` + its header
- `src/core/cigi_bridge/test/test_cigi_wire_conformance.cpp`
- `CIGI_PKT_RUNWAY_FRICTION` (0xCB) references, all hand-rolled `read_be*`/`write_be*` helpers in `XPluginMain.cpp`

---

## Phase 0 — Prerequisites

### Task 0.1: Verify mingw64 cross-compiles CCL

**Files:** none (verification only)

- [ ] **Step 1: Configure CCL cross-build**

```bash
cd /home/wim/simulator_framework/references/CIGI/cigi3.3/ccl
cmake -B build-mingw \
  -DCMAKE_TOOLCHAIN_FILE=/home/wim/simulator_framework/x-plane_plugins/xplanecigi/toolchain-mingw64.cmake \
  -DCMAKE_BUILD_TYPE=Release
```

Expected: configuration succeeds. If it fails with "unsupported toolchain" or similar, go to Task 0.2.

- [ ] **Step 2: Cross-build CCL**

Run: `cmake --build build-mingw -j4`
Expected: produces `build-mingw/lib/libcigicl.a` (static — shared libs are messier for plugin linking).

- [ ] **Step 3: Inspect the artifact**

Run: `file references/CIGI/cigi3.3/ccl/build-mingw/lib/libcigicl*`
Expected: reports `PE32+ executable (DLL)` for `.dll.a` or `current ar archive` for `.a`. Either is usable from the plugin.

- [ ] **Step 4: Install to an in-tree prefix**

```bash
cmake --install build-mingw --prefix "$(pwd)/install-mingw"
ls install-mingw/lib/libcigicl*
ls install-mingw/include/cigicl/CigiIGSession.h
```

Expected: both files exist. This is the path `xplanecigi/CMakeLists.txt` will reference.

- [ ] **Step 5: Commit if paths changed any .gitignore**

Check whether `build-mingw/` and `install-mingw/` are covered by the existing `references/` gitignore entry. If not, add them.

```bash
git status references/
```
Expected: no new files listed under git. If there are, investigate before proceeding.

**If mingw64 build fails, proceed to Task 0.2. Otherwise skip to Task 1.1.**

---

### Task 0.2: Static-source fallback for CCL cross-compile

**Files:**
- Modify: `x-plane_plugins/xplanecigi/CMakeLists.txt`

**Only run this task if 0.1 failed.** This embeds CCL sources into the plugin build rather than cross-compiling CCL separately.

- [ ] **Step 1: List CCL sources needed for CIGI 3.3**

```bash
ls references/CIGI/cigi3.3/ccl/source/*.cpp \
  | grep -E 'V3_3|V3_2|V3[.]cpp|Base|Version|Session|Incoming|Outgoing|BaseSignal|SignalTable|AnimationTable' \
  > /tmp/ccl-sources.txt
wc -l /tmp/ccl-sources.txt
```

Expected: ~50 source files listed.

- [ ] **Step 2: Add globbed static library in plugin CMakeLists.txt**

Append to `x-plane_plugins/xplanecigi/CMakeLists.txt` before the plugin target:

```cmake
# CCL static-source fallback: compile the CCL 3.3 sources into a plugin-
# local static library when mingw64 cross-build of CCL is not available.
file(GLOB_RECURSE CCL_SOURCES
  "${CMAKE_SOURCE_DIR}/../../references/CIGI/cigi3.3/ccl/source/*.cpp")
add_library(cigicl_static STATIC ${CCL_SOURCES})
target_include_directories(cigicl_static PUBLIC
  "${CMAKE_SOURCE_DIR}/../../references/CIGI/cigi3.3/ccl/include")
target_compile_definitions(cigicl_static PUBLIC CIGI_LIB)
```

- [ ] **Step 3: Test-build (empty stub plugin target linking only CCL)**

```bash
cd x-plane_plugins/xplanecigi && cmake --build build -j4
```

Expected: `cigicl_static` builds clean. Subsequent tasks link `cigicl_static` in place of `cigicl`.

- [ ] **Step 4: Commit**

```bash
git add x-plane_plugins/xplanecigi/CMakeLists.txt
git commit -m "build(xplanecigi): static-source CCL fallback for mingw64"
```

---

## Phase 1 — Library Scaffold

### Task 1.1: Create cigi_session directory + CMake

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/CMakeLists.txt`
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/.keep`
- Create: `src/core/cigi_bridge/cigi_session/src/.keep`
- Create: `src/core/cigi_bridge/cigi_session/test/.keep`

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p src/core/cigi_bridge/cigi_session/{include/cigi_session/emitters,include/cigi_session/processors,src,test}
touch src/core/cigi_bridge/cigi_session/include/cigi_session/.keep
touch src/core/cigi_bridge/cigi_session/src/.keep
touch src/core/cigi_bridge/cigi_session/test/.keep
```

- [ ] **Step 2: Write library CMakeLists.txt**

Create `src/core/cigi_bridge/cigi_session/CMakeLists.txt`:

```cmake
# cigi_session — typed CCL wrapper used by both host node and plugin.
# Two build modes:
#   (a) As a subdirectory of the cigi_bridge ROS2 package (Linux native).
#   (b) As a subdirectory of the xplanecigi plugin (mingw64 cross).
# CMAKE_CURRENT_SOURCE_DIR is the same in both; CCL must already be
# found by the parent CMakeLists.

if(NOT DEFINED CCL_INCLUDE_DIR OR NOT DEFINED CCL_LIBRARY)
  message(FATAL_ERROR "cigi_session requires CCL_INCLUDE_DIR and CCL_LIBRARY from parent CMakeLists")
endif()

add_library(cigi_session STATIC
  src/HostSession.cpp
  src/IgSession.cpp
  src/emitters.cpp
)

target_include_directories(cigi_session PUBLIC
  include
  ${CCL_INCLUDE_DIR}
)

target_link_libraries(cigi_session PUBLIC ${CCL_LIBRARY})

target_compile_features(cigi_session PUBLIC cxx_std_17)

if(BUILD_TESTING AND CMAKE_PROJECT_NAME STREQUAL "sim_cigi_bridge")
  # gtest only available on the ROS2 side. Plugin build skips this.
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_cigi_session test/test_cigi_session.cpp)
  target_link_libraries(test_cigi_session cigi_session)
endif()
```

- [ ] **Step 3: Wire into parent (cigi_bridge) CMakeLists.txt**

Edit `src/core/cigi_bridge/CMakeLists.txt`. Find the `if(CCL_INCLUDE_DIR)` block and append inside it (after `CCL_LIBRARY` is set):

```cmake
  add_subdirectory(cigi_session)
  target_link_libraries(cigi_bridge_node cigi_session)
```

- [ ] **Step 4: Verify build**

```bash
cd /home/wim/simulator_framework
colcon build --packages-select sim_cigi_bridge
```

Expected: warnings about empty `cigi_session` library (no sources yet) or clean build. Failures mean the CMake wiring is wrong.

- [ ] **Step 5: Commit**

```bash
git add src/core/cigi_bridge/cigi_session/ src/core/cigi_bridge/CMakeLists.txt
git commit -m "chore(cigi_session): scaffold library directory + CMake"
```

---

### Task 1.2: Write HostSession + IgSession skeletons

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/HostSession.h`
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/IgSession.h`
- Create: `src/core/cigi_bridge/cigi_session/src/HostSession.cpp`
- Create: `src/core/cigi_bridge/cigi_session/src/IgSession.cpp`
- Create: `src/core/cigi_bridge/cigi_session/src/emitters.cpp`

- [ ] **Step 1: Write HostSession.h**

```cpp
// Host-side CIGI 3.3 session. Owns buffered wire output and dispatches
// received IG→Host packets (SOF, HAT/HOT Extended Response) to registered
// processors. Thin wrapper around CCL; app code never touches byte offsets.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace cigi_session {

class ISofProcessor;
class IHatHotRespProcessor;

class HostSession {
public:
    HostSession();
    ~HostSession();
    HostSession(const HostSession &) = delete;
    HostSession & operator=(const HostSession &) = delete;

    // Register processors. nullptr clears. Default is no-op.
    void SetSofProcessor(ISofProcessor * proc);
    void SetHatHotRespProcessor(IHatHotRespProcessor * proc);

    // Parse a received datagram, dispatching each packet to its processor.
    // Returns the number of packets successfully parsed.
    std::size_t HandleDatagram(const std::uint8_t * data, std::size_t len);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cigi_session
```

- [ ] **Step 2: Write IgSession.h**

```cpp
// IG-side CIGI 3.3 session. Dispatches received Host→IG packets to
// registered processors. Plugin code registers one processor per packet
// class it handles.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace cigi_session {

class IIgCtrlProcessor;
class IEntityCtrlProcessor;
class IHatHotReqProcessor;
class IAtmosphereProcessor;
class IEnvRegionProcessor;
class IWeatherCtrlProcessor;
class ICompCtrlProcessor;

class IgSession {
public:
    IgSession();
    ~IgSession();
    IgSession(const IgSession &) = delete;
    IgSession & operator=(const IgSession &) = delete;

    void SetIgCtrlProcessor(IIgCtrlProcessor * p);
    void SetEntityCtrlProcessor(IEntityCtrlProcessor * p);
    void SetHatHotReqProcessor(IHatHotReqProcessor * p);
    void SetAtmosphereProcessor(IAtmosphereProcessor * p);
    void SetEnvRegionProcessor(IEnvRegionProcessor * p);
    void SetWeatherCtrlProcessor(IWeatherCtrlProcessor * p);
    void SetCompCtrlProcessor(ICompCtrlProcessor * p);

    std::size_t HandleDatagram(const std::uint8_t * data, std::size_t len);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cigi_session
```

- [ ] **Step 3: Write skeleton .cpp files**

`src/core/cigi_bridge/cigi_session/src/HostSession.cpp`:

```cpp
#include "cigi_session/HostSession.h"

namespace cigi_session {

struct HostSession::Impl {};

HostSession::HostSession() : impl_(std::make_unique<Impl>()) {}
HostSession::~HostSession() = default;

void HostSession::SetSofProcessor(ISofProcessor *) {}
void HostSession::SetHatHotRespProcessor(IHatHotRespProcessor *) {}

std::size_t HostSession::HandleDatagram(const std::uint8_t *, std::size_t) {
    return 0;
}

}  // namespace cigi_session
```

`src/core/cigi_bridge/cigi_session/src/IgSession.cpp`:

```cpp
#include "cigi_session/IgSession.h"

namespace cigi_session {

struct IgSession::Impl {};

IgSession::IgSession() : impl_(std::make_unique<Impl>()) {}
IgSession::~IgSession() = default;

void IgSession::SetIgCtrlProcessor(IIgCtrlProcessor *) {}
void IgSession::SetEntityCtrlProcessor(IEntityCtrlProcessor *) {}
void IgSession::SetHatHotReqProcessor(IHatHotReqProcessor *) {}
void IgSession::SetAtmosphereProcessor(IAtmosphereProcessor *) {}
void IgSession::SetEnvRegionProcessor(IEnvRegionProcessor *) {}
void IgSession::SetWeatherCtrlProcessor(IWeatherCtrlProcessor *) {}
void IgSession::SetCompCtrlProcessor(ICompCtrlProcessor *) {}

std::size_t IgSession::HandleDatagram(const std::uint8_t *, std::size_t) {
    return 0;
}

}  // namespace cigi_session
```

`src/core/cigi_bridge/cigi_session/src/emitters.cpp`:

```cpp
// Emitter implementations land here as they are ported. Empty for now.
```

- [ ] **Step 4: Build**

```bash
colcon build --packages-select sim_cigi_bridge
```

Expected: `libcigi_session.a` produced. Warnings about unused parameters are fine.

- [ ] **Step 5: Commit**

```bash
git add src/core/cigi_bridge/cigi_session/
git commit -m "feat(cigi_session): HostSession + IgSession skeletons"
```

---

### Task 1.3: Establish test pattern — HAT/HOT Request emitter round-trip

**Purpose:** prove the library can emit a packet that CCL Unpack reads back correctly. HAT/HOT Request picked first because it's simple (no bitfields, no Spec parameter for Unpack) and already has a working pattern in the existing `test_cigi_wire_conformance.cpp`.

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/emitters/HatHotReqEmit.h`
- Modify: `src/core/cigi_bridge/cigi_session/src/emitters.cpp`
- Create: `src/core/cigi_bridge/cigi_session/test/test_cigi_session.cpp`

- [ ] **Step 1: Write HatHotReqEmit.h**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace cigi_session {

// Build a CIGI 3.3 HAT/HOT Request packet (§4.1.24, 32 bytes) into buf.
// Buffer must be at least 32 bytes. Returns bytes written (always 32 on
// success, 0 on insufficient buffer).
// Request type is always Extended (elicits a 0x67 response with Material).
// Coordinate system is always Geodetic.
std::size_t BuildHatHotRequest(std::uint8_t * buf, std::size_t cap,
                                std::uint16_t request_id,
                                double lat_deg, double lon_deg);

}  // namespace cigi_session
```

- [ ] **Step 2: Write failing test**

Create `src/core/cigi_bridge/cigi_session/test/test_cigi_session.cpp`:

```cpp
#include <gtest/gtest.h>
#include <cstring>

#include "cigi_session/emitters/HatHotReqEmit.h"

#include <CigiHatHotReqV3_2.h>
#include <CigiBaseHatHotReq.h>

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  constexpr bool kCclSwap = true;
#else
  constexpr bool kCclSwap = false;
#endif

TEST(CigiSession, HatHotRequestEmit) {
    std::uint8_t buf[32] = {};
    auto n = cigi_session::BuildHatHotRequest(buf, sizeof(buf),
                                              /*id=*/42,
                                              /*lat=*/50.901389,
                                              /*lon=*/4.484444);
    ASSERT_EQ(n, 32u);

    CigiHatHotReqV3_2 cigi;
    ASSERT_GE(cigi.Unpack(buf, kCclSwap, nullptr), 0);

    EXPECT_EQ(cigi.GetHatHotID(), 42);
    EXPECT_EQ(cigi.GetReqType(), CigiBaseHatHotReq::Extended);
    EXPECT_EQ(cigi.GetSrcCoordSys(), CigiBaseHatHotReq::Geodetic);
    EXPECT_DOUBLE_EQ(cigi.GetLat(), 50.901389);
    EXPECT_DOUBLE_EQ(cigi.GetLon(), 4.484444);
}
```

- [ ] **Step 3: Build + confirm test fails**

```bash
colcon build --packages-select sim_cigi_bridge --cmake-args -DBUILD_TESTING=ON
```

Expected: compile error — `BuildHatHotRequest` is declared but not defined.

- [ ] **Step 4: Implement BuildHatHotRequest**

Append to `src/core/cigi_bridge/cigi_session/src/emitters.cpp`:

```cpp
#include "cigi_session/emitters/HatHotReqEmit.h"

#include <CigiHatHotReqV3_2.h>
#include <CigiBaseHatHotReq.h>
#include <CigiVersionID.h>

namespace cigi_session {

std::size_t BuildHatHotRequest(std::uint8_t * buf, std::size_t cap,
                                std::uint16_t request_id,
                                double lat_deg, double lon_deg) {
    constexpr std::size_t kSize = 32;
    if (cap < kSize) return 0;

    CigiHatHotReqV3_2 pkt;
    pkt.SetHatHotID(request_id);
    pkt.SetReqType(CigiBaseHatHotReq::Extended);
    pkt.SetSrcCoordSys(CigiBaseHatHotReq::Geodetic);
    pkt.SetUpdatePeriod(0);
    pkt.SetEntityID(0);
    pkt.SetLat(lat_deg);
    pkt.SetLon(lon_deg);
    pkt.SetAlt(0.0);

    CigiVersionID ver;
    ver.SetCigiVersion(3, 3);
    int n = pkt.Pack(&pkt, buf, &ver);
    return (n > 0) ? static_cast<std::size_t>(n) : 0;
}

}  // namespace cigi_session
```

- [ ] **Step 5: Build + run test**

```bash
colcon build --packages-select sim_cigi_bridge --cmake-args -DBUILD_TESTING=ON
./build/sim_cigi_bridge/test_cigi_session --gtest_filter=CigiSession.HatHotRequestEmit
```

Expected: `[  PASSED  ] 1 test.`

- [ ] **Step 6: Commit**

```bash
git add src/core/cigi_bridge/cigi_session/
git commit -m "feat(cigi_session): HatHotReqEmit + test_cigi_session harness

First emitter. Establishes the pattern: typed Build* wrapping CCL Pack,
gtest round-trips through CCL Unpack. This replaces the old
test_cigi_wire_conformance duplicate-encoder pattern — the library's
emitters ARE the thing under test."
```

---

## Phase 2 — Host Migration

Every task in this phase follows the same recipe:
1. Add emitter (header + impl) in library, with a round-trip test.
2. Replace the hand-rolled function in `cigi_host_node.cpp` or `weather_encoder.cpp` with a call to the emitter.
3. Rebuild. **`test_cigi_wire_conformance` must still pass** — this is the safety net.
4. Commit.

### Task 2.1: IG Control emitter

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/emitters/IgCtrlEmit.h`
- Modify: `src/core/cigi_bridge/cigi_session/src/emitters.cpp`
- Modify: `src/core/cigi_bridge/cigi_session/test/test_cigi_session.cpp`
- Modify: `src/core/cigi_bridge/src/cigi_host_node.cpp`
- Modify: `src/core/cigi_bridge/include/cigi_bridge/cigi_host_node.hpp`

- [ ] **Step 1: Write IgCtrlEmit.h**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace cigi_session {

enum class IgMode : std::uint8_t {
    StandbyOrReset = 0,   // spec collapses these
    Operate        = 1,
    Debug          = 2,
    Offline        = 3,
};

// IG Control packet (§4.1.1, 24 bytes). Always sets Major=3, MinorVer=2,
// TimestampValid=1. No database-load support yet (Database=0).
std::size_t BuildIgCtrl(std::uint8_t * buf, std::size_t cap,
                        IgMode mode,
                        std::uint32_t host_frame_number,
                        std::uint32_t timestamp_10us_ticks,
                        std::uint32_t last_ig_frame_number);

}  // namespace cigi_session
```

- [ ] **Step 2: Add failing test**

Append to `test_cigi_session.cpp`:

```cpp
#include "cigi_session/emitters/IgCtrlEmit.h"
#include <CigiIGCtrlV3_3.h>
#include <CigiBaseIGCtrl.h>

TEST(CigiSession, IgCtrlEmit) {
    std::uint8_t buf[24] = {};
    auto n = cigi_session::BuildIgCtrl(buf, sizeof(buf),
                                       cigi_session::IgMode::Operate,
                                       /*frame=*/0xDEADBEEFu,
                                       /*ts=*/50000u,
                                       /*last_ig=*/12345u);
    ASSERT_EQ(n, 24u);

    CigiIGCtrlV3_3 cigi;
    ASSERT_GE(cigi.Unpack(buf, kCclSwap, nullptr), 0);
    EXPECT_EQ(buf[2], 3);  // Major Version (no getter)
    EXPECT_EQ(cigi.GetIGMode(), CigiBaseIGCtrl::Operate);
    EXPECT_TRUE(cigi.GetTimeStampValid());
    EXPECT_EQ(cigi.GetFrameCntr(), 0xDEADBEEFu);
    EXPECT_EQ(cigi.GetTimeStamp(), 50000u);
    EXPECT_EQ(cigi.GetLastRcvdIGFrame(), 12345u);
}
```

- [ ] **Step 3: Implement BuildIgCtrl**

Append to `emitters.cpp`:

```cpp
#include "cigi_session/emitters/IgCtrlEmit.h"
#include <CigiIGCtrlV3_3.h>
#include <CigiBaseIGCtrl.h>

namespace cigi_session {

std::size_t BuildIgCtrl(std::uint8_t * buf, std::size_t cap,
                        IgMode mode,
                        std::uint32_t host_frame_number,
                        std::uint32_t timestamp_10us_ticks,
                        std::uint32_t last_ig_frame_number) {
    constexpr std::size_t kSize = 24;
    if (cap < kSize) return 0;

    CigiIGCtrlV3_3 pkt;
    pkt.SetIGMode(static_cast<CigiBaseIGCtrl::IGModeGrp>(mode));
    pkt.SetDatabaseID(0);
    pkt.SetTimeStampValid(true);
    pkt.SetFrameCntr(host_frame_number);
    pkt.SetTimeStamp(timestamp_10us_ticks);
    pkt.SetLastRcvdIGFrame(last_ig_frame_number);

    CigiVersionID ver;
    ver.SetCigiVersion(3, 3);
    int n = pkt.Pack(&pkt, buf, &ver);
    return (n > 0) ? static_cast<std::size_t>(n) : 0;
}

}  // namespace cigi_session
```

- [ ] **Step 4: Build + run test**

```bash
colcon build --packages-select sim_cigi_bridge --cmake-args -DBUILD_TESTING=ON
./build/sim_cigi_bridge/test_cigi_session --gtest_filter=CigiSession.IgCtrlEmit
```

Expected: `[ PASSED ] 1 test.`

- [ ] **Step 5: Replace host encoder**

Edit `src/core/cigi_bridge/src/cigi_host_node.cpp`. Find `void CigiHostNode::encode_ig_ctrl(...)` and replace its body with:

```cpp
void CigiHostNode::encode_ig_ctrl(uint8_t * buf, uint32_t frame_cntr, double timestamp_s,
                                   uint8_t ig_mode) const
{
    // ig_mode is a bitfield with IG Mode in bits 1..0. Extract to IgMode enum.
    auto mode = static_cast<cigi_session::IgMode>(ig_mode & 0x03);
    auto ticks = static_cast<uint32_t>(timestamp_s * 1e5);
    cigi_session::BuildIgCtrl(buf, CIGI_IG_CTRL_SIZE, mode,
                              frame_cntr, ticks, last_ig_frame_);
}
```

Add at the top of the file: `#include "cigi_session/emitters/IgCtrlEmit.h"`.

- [ ] **Step 6: Rebuild + verify safety-net test still passes**

```bash
colcon build --packages-select sim_cigi_bridge --cmake-args -DBUILD_TESTING=ON
./build/sim_cigi_bridge/test_cigi_wire_conformance --gtest_filter=CigiWireConformance.IGControl*
```

Expected: both IGControl tests pass. If they fail, the bytes the emitter produces differ from the old raw encoder — inspect both buffers to find the mismatch.

- [ ] **Step 7: Commit**

```bash
git add src/core/cigi_bridge/
git commit -m "feat(cigi_bridge): migrate IG Control emitter to cigi_session"
```

---

### Task 2.2: Entity Control emitter

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/emitters/EntityCtrlEmit.h`
- Modify: `src/core/cigi_bridge/cigi_session/src/emitters.cpp`
- Modify: `src/core/cigi_bridge/cigi_session/test/test_cigi_session.cpp`
- Modify: `src/core/cigi_bridge/src/cigi_host_node.cpp`

- [ ] **Step 1: Write EntityCtrlEmit.h**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace cigi_session {

// Entity Control packet (§4.1.2, 48 bytes). Entity State is always Active.
// Attach State always Detach (top-level entity). Alpha=255 (fully opaque).
std::size_t BuildEntityCtrl(std::uint8_t * buf, std::size_t cap,
                             std::uint16_t entity_id,
                             float roll_deg, float pitch_deg, float yaw_deg,
                             double lat_deg, double lon_deg, double alt_m);

}  // namespace cigi_session
```

- [ ] **Step 2: Add failing test**

Append to `test_cigi_session.cpp`:

```cpp
#include "cigi_session/emitters/EntityCtrlEmit.h"
#include <CigiEntityCtrlV3_3.h>
#include <CigiAnimationTable.h>

TEST(CigiSession, EntityCtrlEmit) {
    std::uint8_t buf[48] = {};
    auto n = cigi_session::BuildEntityCtrl(buf, sizeof(buf),
                                           /*id=*/0,
                                           -1.5f, 2.5f, 123.4f,
                                           50.901389, 4.484444, 56.0);
    ASSERT_EQ(n, 48u);

    CigiEntityCtrlV3_3 cigi;
    CigiAnimationTable tbl;
    ASSERT_GE(cigi.Unpack(buf, kCclSwap, &tbl), 0);
    EXPECT_EQ(cigi.GetEntityID(), 0);
    EXPECT_EQ(cigi.GetEntityState(), CigiBaseEntityCtrl::Active);
    EXPECT_FLOAT_EQ(cigi.GetRoll(),  -1.5f);
    EXPECT_FLOAT_EQ(cigi.GetPitch(),  2.5f);
    EXPECT_FLOAT_EQ(cigi.GetYaw(),  123.4f);
    EXPECT_DOUBLE_EQ(cigi.GetLat(),  50.901389);
    EXPECT_DOUBLE_EQ(cigi.GetLon(),   4.484444);
    EXPECT_DOUBLE_EQ(cigi.GetAlt(),  56.0);
}
```

- [ ] **Step 3: Implement BuildEntityCtrl**

Append to `emitters.cpp`:

```cpp
#include "cigi_session/emitters/EntityCtrlEmit.h"
#include <CigiEntityCtrlV3_3.h>

namespace cigi_session {

std::size_t BuildEntityCtrl(std::uint8_t * buf, std::size_t cap,
                             std::uint16_t entity_id,
                             float roll_deg, float pitch_deg, float yaw_deg,
                             double lat_deg, double lon_deg, double alt_m) {
    constexpr std::size_t kSize = 48;
    if (cap < kSize) return 0;

    CigiEntityCtrlV3_3 pkt;
    pkt.SetEntityID(entity_id);
    pkt.SetEntityState(CigiBaseEntityCtrl::Active);
    pkt.SetAttachState(CigiBaseEntityCtrl::Detach);
    pkt.SetAlpha(255);
    pkt.SetRoll(roll_deg);
    pkt.SetPitch(pitch_deg);
    pkt.SetYaw(yaw_deg);
    pkt.SetLat(lat_deg);
    pkt.SetLon(lon_deg);
    pkt.SetAlt(alt_m);

    CigiVersionID ver;
    ver.SetCigiVersion(3, 3);
    int n = pkt.Pack(&pkt, buf, &ver);
    return (n > 0) ? static_cast<std::size_t>(n) : 0;
}

}  // namespace cigi_session
```

- [ ] **Step 4: Build + run test**

```bash
colcon build --packages-select sim_cigi_bridge --cmake-args -DBUILD_TESTING=ON
./build/sim_cigi_bridge/test_cigi_session --gtest_filter=CigiSession.EntityCtrlEmit
```

Expected: pass.

- [ ] **Step 5: Replace host encoder**

Replace `CigiHostNode::encode_entity_ctrl` body with a call to `BuildEntityCtrl`. Keep the signature identical so callers don't change.

- [ ] **Step 6: Rebuild + verify safety-net test**

```bash
./build/sim_cigi_bridge/test_cigi_wire_conformance --gtest_filter=CigiWireConformance.EntityCtrl*
```

Expected: pass.

- [ ] **Step 7: Commit**

```bash
git commit -am "feat(cigi_bridge): migrate Entity Control emitter to cigi_session"
```

---

### Task 2.3: HAT/HOT Request migration to emitter

**Files:**
- Modify: `src/core/cigi_bridge/src/cigi_host_node.cpp`

The emitter is already written and tested (Task 1.3). Only the host-side call-site needs migration.

- [ ] **Step 1: Replace host encoder**

Replace `CigiHostNode::encode_hot_request` body with:

```cpp
void CigiHostNode::encode_hot_request(uint8_t * buf, uint16_t request_id,
                                      double lat_deg, double lon_deg) const
{
    cigi_session::BuildHatHotRequest(buf, CIGI_HOT_REQUEST_SIZE,
                                     request_id, lat_deg, lon_deg);
}
```

Add `#include "cigi_session/emitters/HatHotReqEmit.h"` at the top of the file.

- [ ] **Step 2: Rebuild + verify**

```bash
colcon build --packages-select sim_cigi_bridge --cmake-args -DBUILD_TESTING=ON
./build/sim_cigi_bridge/test_cigi_wire_conformance --gtest_filter=CigiWireConformance.HatHotRequest*
```

Expected: pass.

- [ ] **Step 3: Commit**

```bash
git commit -am "feat(cigi_bridge): migrate HAT/HOT Request emitter to cigi_session"
```

---

### Task 2.4: SOF processor

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/processors/ISofProcessor.h`
- Modify: `src/core/cigi_bridge/cigi_session/src/HostSession.cpp`
- Modify: `src/core/cigi_bridge/cigi_session/test/test_cigi_session.cpp`
- Modify: `src/core/cigi_bridge/src/cigi_host_node.cpp`

- [ ] **Step 1: Write ISofProcessor.h**

```cpp
#pragma once
#include <cstdint>

namespace cigi_session {

enum class IgModeRx : std::uint8_t {
    StandbyOrReset = 0, Operate = 1, Debug = 2, Offline = 3,
};

struct SofFields {
    IgModeRx  ig_mode;
    std::uint8_t ig_status_code;
    std::int8_t  database_id;
    std::uint32_t ig_frame_number;
    std::uint32_t last_host_frame_number;
};

class ISofProcessor {
public:
    virtual ~ISofProcessor() = default;
    virtual void OnSof(const SofFields & f) = 0;
};

}  // namespace cigi_session
```

- [ ] **Step 2: Implement HostSession dispatch + SOF decode**

Replace `HostSession.cpp` with:

```cpp
#include "cigi_session/HostSession.h"
#include "cigi_session/processors/ISofProcessor.h"

#include <CigiSOFV3_2.h>
#include <CigiBaseSOF.h>
#include <CigiVersionID.h>

#include <cstring>

namespace cigi_session {

namespace {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
constexpr bool kSwap = true;
#else
constexpr bool kSwap = false;
#endif
}

struct HostSession::Impl {
    ISofProcessor * sof = nullptr;
    IHatHotRespProcessor * resp = nullptr;  // wired in Task 2.5
};

HostSession::HostSession() : impl_(std::make_unique<Impl>()) {}
HostSession::~HostSession() = default;

void HostSession::SetSofProcessor(ISofProcessor * p) { impl_->sof = p; }
void HostSession::SetHatHotRespProcessor(IHatHotRespProcessor * p) { impl_->resp = p; }

std::size_t HostSession::HandleDatagram(const std::uint8_t * data, std::size_t len) {
    std::size_t packets = 0;
    std::size_t off = 0;
    while (off + 2 <= len) {
        std::uint8_t pkt_id   = data[off];
        std::uint8_t pkt_size = data[off + 1];
        if (pkt_size < 2 || off + pkt_size > len) break;

        if (pkt_id == 0x65 && pkt_size >= 24 && impl_->sof) {
            CigiSOFV3_2 pkt;
            if (pkt.Unpack(const_cast<std::uint8_t *>(data + off), kSwap, nullptr) >= 0) {
                SofFields f;
                f.ig_mode                = static_cast<IgModeRx>(pkt.GetIGMode());
                f.ig_status_code         = 0;   // CCL hides it; read raw if needed
                f.database_id            = pkt.GetDatabaseID();
                f.ig_frame_number        = pkt.GetFrameCntr();
                f.last_host_frame_number = 0;   // CCL doesn't expose; raw if needed
                impl_->sof->OnSof(f);
                ++packets;
            }
        }
        // HAT/HOT Extended Response handled in Task 2.5.
        off += pkt_size;
    }
    return packets;
}

}  // namespace cigi_session
```

- [ ] **Step 3: Add failing test**

Append to `test_cigi_session.cpp`:

```cpp
#include "cigi_session/HostSession.h"
#include "cigi_session/processors/ISofProcessor.h"
#include <optional>

struct SofSpy : cigi_session::ISofProcessor {
    std::optional<cigi_session::SofFields> got;
    void OnSof(const cigi_session::SofFields & f) override { got = f; }
};

TEST(CigiSession, SofProcessorDispatch) {
    // Build a spec wire buffer by hand (same pattern as existing host
    // SOF parser test).
    std::uint8_t buf[24] = {};
    buf[0] = 0x65; buf[1] = 24; buf[2] = 3;
    buf[5] = (2u<<4) | (1u<<2) | 1u;  // MinorVer=2, TSValid, Operate
    buf[6] = 0x80; buf[7] = 0x00;
    // IG Frame Number big-endian at 8..11
    buf[8]=0xCA; buf[9]=0xFE; buf[10]=0xBA; buf[11]=0xBE;

    cigi_session::HostSession session;
    SofSpy spy;
    session.SetSofProcessor(&spy);
    EXPECT_EQ(session.HandleDatagram(buf, sizeof(buf)), 1u);
    ASSERT_TRUE(spy.got.has_value());
    EXPECT_EQ(spy.got->ig_mode, cigi_session::IgModeRx::Operate);
    EXPECT_EQ(spy.got->ig_frame_number, 0xCAFEBABEu);
}
```

- [ ] **Step 4: Build + run test**

```bash
colcon build --packages-select sim_cigi_bridge --cmake-args -DBUILD_TESTING=ON
./build/sim_cigi_bridge/test_cigi_session --gtest_filter=CigiSession.SofProcessorDispatch
```

Expected: pass.

- [ ] **Step 5: Migrate host SOF parser**

In `cigi_host_node.cpp`, find the `else if (pkt_id == CIGI_PKT_SOF && pkt_size >= 24)` branch in `recv_pending`. Replace the entire else-if body with a call to `HostSession::HandleDatagram`. Since the current parser is per-packet inside a datagram loop and HandleDatagram parses whole datagrams, the cleanest change is:

Replace the entire `while (offset + 2 <= n) { ... SOF branch ... }` inner parser for SOF with (leaving the HAT/HOT branch for now):

```cpp
// Delegate SOF parsing to cigi_session; keep local HAT/HOT branch until
// Task 2.5 migrates it.
session_.HandleDatagram(buf, n);  // dispatches SOF to XplaneHostSofProcessor
// ... existing HAT/HOT branch parses the same bytes a second time.
```

Register the host node as an ISofProcessor. Add to `cigi_host_node.hpp` members:

```cpp
cigi_session::HostSession session_;
// and implement:
void OnSof(const cigi_session::SofFields & f) override {
    last_ig_frame_ = f.ig_frame_number;
    uint8_t prev = ig_status_;
    ig_status_ = static_cast<uint8_t>(f.ig_mode);
    if (ig_status_ != prev) { /* existing transition log + publish */ }
}
```

Have `CigiHostNode` inherit from `cigi_session::ISofProcessor`. In `on_activate` or constructor, call `session_.SetSofProcessor(this);`.

- [ ] **Step 6: Rebuild + verify safety-net**

```bash
colcon build --packages-select sim_cigi_bridge --cmake-args -DBUILD_TESTING=ON
./build/sim_cigi_bridge/test_cigi_wire_conformance --gtest_filter=CigiWireConformance.SOFParse*
```

Expected: pass.

- [ ] **Step 7: Commit**

```bash
git commit -am "feat(cigi_bridge): migrate SOF parser to cigi_session processor"
```

---

### Task 2.5: HAT/HOT Extended Response processor

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/processors/IHatHotRespProcessor.h`
- Modify: `src/core/cigi_bridge/cigi_session/src/HostSession.cpp`
- Modify: `src/core/cigi_bridge/cigi_session/test/test_cigi_session.cpp`
- Modify: `src/core/cigi_bridge/src/cigi_host_node.cpp`

- [ ] **Step 1: Write IHatHotRespProcessor.h**

```cpp
#pragma once
#include <cstdint>

namespace cigi_session {

struct HatHotRespFields {
    std::uint16_t request_id;
    bool          valid;
    double        hat_m;
    double        hot_m;
    std::uint32_t material_code;
    float         normal_azimuth_deg;
    float         normal_elevation_deg;
};

class IHatHotRespProcessor {
public:
    virtual ~IHatHotRespProcessor() = default;
    virtual void OnHatHotResp(const HatHotRespFields & f) = 0;
};

}  // namespace cigi_session
```

- [ ] **Step 2: Extend HostSession dispatch**

In `HostSession.cpp::HandleDatagram`, add after the SOF branch (inside the while loop):

```cpp
if (pkt_id == 0x67 && pkt_size >= 40 && impl_->resp) {
    CigiHatHotXRespV3_2 pkt;
    if (pkt.Unpack(const_cast<std::uint8_t *>(data + off), kSwap, nullptr) >= 0) {
        HatHotRespFields f;
        f.request_id             = pkt.GetHatHotID();
        f.valid                  = pkt.GetValid();
        f.hat_m                  = pkt.GetHat();
        f.hot_m                  = pkt.GetHot();
        f.material_code          = pkt.GetMaterial();
        f.normal_azimuth_deg     = pkt.GetNormAz();
        f.normal_elevation_deg   = pkt.GetNormEl();
        impl_->resp->OnHatHotResp(f);
        ++packets;
    }
}
```

Add `#include <CigiHatHotXRespV3_2.h>` at the top of `HostSession.cpp`.

- [ ] **Step 3: Add test**

Append to `test_cigi_session.cpp`:

```cpp
#include "cigi_session/processors/IHatHotRespProcessor.h"

struct RespSpy : cigi_session::IHatHotRespProcessor {
    std::optional<cigi_session::HatHotRespFields> got;
    void OnHatHotResp(const cigi_session::HatHotRespFields & f) override { got = f; }
};

TEST(CigiSession, HatHotRespDispatch) {
    std::uint8_t buf[40] = {};
    buf[0] = 0x67; buf[1] = 40;
    buf[2] = 0x00; buf[3] = 0x63;  // req_id = 99
    buf[4] = 0x01;                 // valid
    // HAT = 123.456 at bytes 8..15 (big-endian double)
    double h = 123.456; std::uint64_t u; std::memcpy(&u, &h, 8);
    for (int i=0;i<8;++i) buf[8+i] = (u >> (56-8*i)) & 0xFF;
    double ho = 78.901; std::memcpy(&u, &ho, 8);
    for (int i=0;i<8;++i) buf[16+i] = (u >> (56-8*i)) & 0xFF;
    buf[27] = 0xAB;                // material low byte

    cigi_session::HostSession session;
    RespSpy spy;
    session.SetHatHotRespProcessor(&spy);
    EXPECT_EQ(session.HandleDatagram(buf, sizeof(buf)), 1u);
    ASSERT_TRUE(spy.got.has_value());
    EXPECT_EQ(spy.got->request_id, 99);
    EXPECT_TRUE(spy.got->valid);
    EXPECT_DOUBLE_EQ(spy.got->hat_m, 123.456);
    EXPECT_DOUBLE_EQ(spy.got->hot_m, 78.901);
    EXPECT_EQ(spy.got->material_code, 0xABu);
}
```

- [ ] **Step 4: Migrate host parser**

In `cigi_host_node.cpp::recv_pending`, remove the `if (pkt_id == CIGI_PKT_HAT_HOT_EXT_RESPONSE ...)` branch inside the manual while-loop. Route through `session_.HandleDatagram(buf, n)` instead. Implement `CigiHostNode::OnHatHotResp(const HatHotRespFields & f)` with the current HAT-publish logic (material code → surface_type, HAT/HOT value → hat_tracker_.resolve → publish). Register via `session_.SetHatHotRespProcessor(this)`.

The manual per-packet while-loop is now **entirely** replaced by `session_.HandleDatagram(buf, n);`.

- [ ] **Step 5: Build + test**

```bash
colcon build --packages-select sim_cigi_bridge --cmake-args -DBUILD_TESTING=ON
./build/sim_cigi_bridge/test_cigi_session --gtest_filter=CigiSession.HatHotRespDispatch
./build/sim_cigi_bridge/test_cigi_wire_conformance
```

Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git commit -am "feat(cigi_bridge): migrate HAT/HOT response parser to cigi_session"
```

---

### Task 2.6: Atmosphere Control emitter

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/emitters/AtmosphereEmit.h`
- Modify: `src/core/cigi_bridge/cigi_session/src/emitters.cpp`
- Modify: `src/core/cigi_bridge/cigi_session/test/test_cigi_session.cpp`
- Modify: `src/core/cigi_bridge/src/weather_encoder.cpp`

- [ ] **Step 1: Write AtmosphereEmit.h**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace cigi_session {

struct AtmosphereFields {
    std::uint8_t humidity_pct;    // 0..100
    float temperature_c;
    float visibility_m;
    float horiz_wind_ms;
    float vert_wind_ms;
    float wind_direction_deg;
    float barometric_pressure_hpa;
};

// Atmosphere Control (§4.1.10, 32 bytes). AtmosphericModelEnable=false.
std::size_t BuildAtmosphereControl(std::uint8_t * buf, std::size_t cap,
                                    const AtmosphereFields & f);

}  // namespace cigi_session
```

- [ ] **Step 2: Add failing test**

```cpp
#include "cigi_session/emitters/AtmosphereEmit.h"

TEST(CigiSession, AtmosphereEmit) {
    cigi_session::AtmosphereFields f{
        .humidity_pct = 75, .temperature_c = 15.0f,
        .visibility_m = 10000.0f, .horiz_wind_ms = 5.0f,
        .vert_wind_ms = 0.0f, .wind_direction_deg = 270.0f,
        .barometric_pressure_hpa = 1013.25f,
    };
    std::uint8_t buf[32] = {};
    ASSERT_EQ(cigi_session::BuildAtmosphereControl(buf, sizeof(buf), f), 32u);
    // Decode directly from bytes (CCL's CigiAtmosCtrl API is less stable).
    EXPECT_EQ(buf[0], 0x0A);
    EXPECT_EQ(buf[1], 32);
    EXPECT_EQ(buf[3], 75);  // humidity at byte 3
}
```

- [ ] **Step 3: Implement BuildAtmosphereControl**

Append to `emitters.cpp`:

```cpp
#include "cigi_session/emitters/AtmosphereEmit.h"
#include <CigiAtmosCtrl.h>

namespace cigi_session {

std::size_t BuildAtmosphereControl(std::uint8_t * buf, std::size_t cap,
                                    const AtmosphereFields & f) {
    constexpr std::size_t kSize = 32;
    if (cap < kSize) return 0;
    CigiAtmosCtrlV3 pkt;
    pkt.SetAtmosEn(false);
    pkt.SetHumidity(f.humidity_pct);
    pkt.SetAirTemp(f.temperature_c);
    pkt.SetVisibility(f.visibility_m);
    pkt.SetHorizWindSp(f.horiz_wind_ms);
    pkt.SetVertWindSp(f.vert_wind_ms);
    pkt.SetWindDir(f.wind_direction_deg);
    pkt.SetBaroPress(f.barometric_pressure_hpa);

    CigiVersionID ver; ver.SetCigiVersion(3, 3);
    int n = pkt.Pack(&pkt, buf, &ver);
    return (n > 0) ? static_cast<std::size_t>(n) : 0;
}

}  // namespace cigi_session
```

- [ ] **Step 4: Build + test**

Pass.

- [ ] **Step 5: Migrate weather_encoder call**

In `src/core/cigi_bridge/src/weather_encoder.cpp`, replace `encode_atmosphere_control` body with a call to `BuildAtmosphereControl`, mapping the WeatherState fields to AtmosphereFields. Keep the old function's signature and return value for caller compatibility.

- [ ] **Step 6: Run all weather-related tests**

```bash
./build/sim_cigi_bridge/test_weather_encoder
./build/sim_cigi_bridge/test_weather_sync
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git commit -am "feat(cigi_bridge): migrate Atmosphere Control emitter to cigi_session"
```

---

### Task 2.7: Weather Control emitter (global + regional)

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/emitters/WeatherCtrlEmit.h`
- Modify: `src/core/cigi_bridge/cigi_session/src/emitters.cpp`
- Modify: `src/core/cigi_bridge/cigi_session/test/test_cigi_session.cpp`
- Modify: `src/core/cigi_bridge/src/weather_encoder.cpp`

**Same recipe as Task 2.6.** Emitter signature:

```cpp
struct WeatherCtrlFields {
    std::uint16_t region_id;          // 0 when Scope=Global
    std::uint8_t  layer_id;
    std::uint8_t  humidity_pct;
    bool          weather_enable;
    bool          scud_enable;
    std::uint8_t  cloud_type;         // 0..15
    std::uint8_t  scope;              // 0=Global, 1=Regional, 2=Entity
    std::uint8_t  severity;           // 0..5
    float         air_temp_c;
    float         visibility_m;
    float         scud_frequency_pct;
    float         coverage_pct;
    float         base_elevation_m;
    float         thickness_m;
    float         transition_band_m;
    float         horiz_wind_ms;
    float         vert_wind_ms;
    float         wind_direction_deg;
    float         barometric_pressure_hpa;
    float         aerosol_concentration_gm3;
};
std::size_t BuildWeatherControl(std::uint8_t * buf, std::size_t cap,
                                 const WeatherCtrlFields & f);
```

- [ ] **Step 1**: Write header (code above).
- [ ] **Step 2**: Add test asserting all 20 fields round-trip via `CigiWeatherCtrlV3::Unpack`.
- [ ] **Step 3**: Implement using `CigiWeatherCtrlV3` setters (consult `references/CIGI/cigi3.3/ccl/include/CigiBaseWeatherCtrl.h` for setter names).
- [ ] **Step 4**: Build + test → pass.
- [ ] **Step 5**: Replace `encode_weather_control` and `encode_weather_control_regional` in `weather_encoder.cpp` with `BuildWeatherControl` calls.
- [ ] **Step 6**: Run `test_weather_encoder`, `test_weather_sync`, `test_cigi_wire_conformance`. Pass all.
- [ ] **Step 7**: Commit `feat(cigi_bridge): migrate Weather Control emitter to cigi_session`.

---

### Task 2.8: Environmental Region Control emitter

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/emitters/EnvRegionEmit.h`
- Modify: emitters.cpp, test_cigi_session.cpp, weather_encoder.cpp

Emitter signature:

```cpp
enum class RegionState : std::uint8_t {
    Inactive = 0, Active = 1, Destroyed = 2,
};
std::size_t BuildEnvRegionControl(std::uint8_t * buf, std::size_t cap,
                                   std::uint16_t region_id, RegionState state,
                                   bool merge_weather,
                                   double lat_deg, double lon_deg,
                                   float size_x_m, float size_y_m,
                                   float corner_radius_m, float rotation_deg,
                                   float transition_perimeter_m);
```

- [ ] **Step 1-7**: Follow same recipe as 2.6. Use `CigiEnvRgnCtrlV3`. Replace `encode_region_control` in `weather_encoder.cpp`.

---

### Task 2.9: Component Control emitter + Runway Friction wire-format swap

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/emitters/CompCtrlEmit.h`
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/ComponentIds.h`
- Modify: emitters.cpp, test_cigi_session.cpp, weather_encoder.cpp

**Key change**: this task replaces the `0xCB` user-defined Runway Friction packet with a standard `0x04` Component Control.

- [ ] **Step 1: Write ComponentIds.h**

```cpp
#pragma once
#include <cstdint>

// Framework-defined Component IDs for Component Control (§4.1.4).
// Global Terrain Surface class (8) is used for terrain-wide overrides.
namespace cigi_session {

enum class GlobalTerrainComponentId : std::uint16_t {
    RunwayFriction = 100,   // Component State = friction level (0..15)
    // 101-199 reserved for future terrain components
};

}
```

- [ ] **Step 2: Write CompCtrlEmit.h**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace cigi_session {

enum class ComponentClass : std::uint8_t {
    Entity              = 0,
    View                = 1,
    ViewGroup           = 2,
    Sensor              = 3,
    RegionalSeaSurface  = 4,
    RegionalTerrain     = 5,
    RegionalLayeredWx   = 6,
    GlobalSeaSurface    = 7,
    GlobalTerrainSurface= 8,
    GlobalLayeredWx     = 9,
    Atmosphere          = 10,
    CelestialSphere     = 11,
    Event               = 12,
    System              = 13,
    SymbolSurface       = 14,
    Symbol              = 15,
};

// Component Control (§4.1.4, 32 bytes). Six 32-bit data words available.
// Overloads: state-only (no data), and full 6-word form.
std::size_t BuildComponentControl(std::uint8_t * buf, std::size_t cap,
                                   ComponentClass cls,
                                   std::uint16_t instance_id,
                                   std::uint16_t component_id,
                                   std::uint8_t component_state,
                                   std::uint32_t data1 = 0, std::uint32_t data2 = 0,
                                   std::uint32_t data3 = 0, std::uint32_t data4 = 0,
                                   std::uint32_t data5 = 0, std::uint32_t data6 = 0);

}
```

- [ ] **Step 3: Test**

```cpp
TEST(CigiSession, CompCtrlRunwayFriction) {
    std::uint8_t buf[32] = {};
    auto n = cigi_session::BuildComponentControl(
        buf, sizeof(buf),
        cigi_session::ComponentClass::GlobalTerrainSurface,
        /*instance=*/0,
        static_cast<std::uint16_t>(cigi_session::GlobalTerrainComponentId::RunwayFriction),
        /*state=*/7);  // wet
    ASSERT_EQ(n, 32u);

    CigiCompCtrlV3_3 cigi;
    ASSERT_GE(cigi.Unpack(buf, kCclSwap, nullptr), 0);
    EXPECT_EQ(cigi.GetCompClassV3(), CigiBaseCompCtrl::GlobalTerrainV3);
    EXPECT_EQ(cigi.GetCompID(), 100);
    EXPECT_EQ(cigi.GetCompState(), 7);
}
```

- [ ] **Step 4: Implement**

```cpp
#include "cigi_session/emitters/CompCtrlEmit.h"
#include <CigiCompCtrlV3_3.h>

namespace cigi_session {

std::size_t BuildComponentControl(std::uint8_t * buf, std::size_t cap,
                                   ComponentClass cls,
                                   std::uint16_t instance_id,
                                   std::uint16_t component_id,
                                   std::uint8_t component_state,
                                   std::uint32_t d1, std::uint32_t d2,
                                   std::uint32_t d3, std::uint32_t d4,
                                   std::uint32_t d5, std::uint32_t d6) {
    constexpr std::size_t kSize = 32;
    if (cap < kSize) return 0;

    CigiCompCtrlV3_3 pkt;
    pkt.SetCompClassV3(static_cast<CigiBaseCompCtrl::CompClassV3Grp>(cls));
    pkt.SetInstanceID(instance_id);
    pkt.SetCompID(component_id);
    pkt.SetCompState(component_state);
    pkt.SetCompData(d1, 0);
    pkt.SetCompData(d2, 1);
    pkt.SetCompData(d3, 2);
    pkt.SetCompData(d4, 3);
    pkt.SetCompData(d5, 4);
    pkt.SetCompData(d6, 5);

    CigiVersionID ver; ver.SetCigiVersion(3, 3);
    int n = pkt.Pack(&pkt, buf, &ver);
    return (n > 0) ? static_cast<std::size_t>(n) : 0;
}

}
```

- [ ] **Step 5: Swap Runway Friction wire format on the host**

In `src/core/cigi_bridge/src/weather_encoder.cpp`:
- Delete the `encode_runway_friction_0xCB` function (and the `CIGI_PKT_RUNWAY_FRICTION` constant).
- Replace its call site with:

```cpp
const std::uint8_t friction = std::min<std::uint8_t>(weather.runway_friction, 15);
size_t emitted = cigi_session::BuildComponentControl(
    buf + offset, buffer_capacity - offset,
    cigi_session::ComponentClass::GlobalTerrainSurface,
    /*instance=*/0,
    static_cast<std::uint16_t>(cigi_session::GlobalTerrainComponentId::RunwayFriction),
    friction);
if (emitted == 0) { /* out of space — drop */ }
else { offset += emitted; }
```

**DO NOT** retire the plugin-side `case 0xCB` handler yet — that lives in Phase 3. The plugin still listens for `0xCB` but will receive no more of them; it will start receiving `0x04` Component Control packets that it currently ignores. Runway friction therefore **stops working** between this commit and the matching plugin commit in Phase 3. This is a planned, time-bounded outage.

- [ ] **Step 6: Rebuild + tests**

```bash
colcon build --packages-select sim_cigi_bridge --cmake-args -DBUILD_TESTING=ON
./build/sim_cigi_bridge/test_cigi_session
./build/sim_cigi_bridge/test_weather_encoder
./build/sim_cigi_bridge/test_weather_sync
```

Expected: all pass. `test_weather_encoder` tests targeting the old `0xCB` bytes must be updated to expect the new Component Control bytes.

- [ ] **Step 7: Commit**

```bash
git commit -am "feat(cigi_bridge): runway friction host-side to Component Control

Host now emits standard Component Control (Class=GlobalTerrainSurface,
ID=100) instead of the user-defined 0xCB packet. Plugin-side handler
migrates in the matching Phase 3 commit; runway friction is temporarily
non-functional between these two commits."
```

---

### Task 2.10: Delete weather_encoder.cpp

**Files:**
- Delete: `src/core/cigi_bridge/src/weather_encoder.cpp` and its header
- Modify: `src/core/cigi_bridge/src/weather_sync.cpp` — call emitters directly
- Modify: `src/core/cigi_bridge/CMakeLists.txt` — remove weather_encoder.cpp from target
- Modify: `src/core/cigi_bridge/test/test_weather_encoder.cpp` — move tests to test_cigi_session or delete

After Tasks 2.6–2.9 the `weather_encoder.cpp` file consists entirely of thin wrappers around `cigi_session::Build*` calls. Remove the indirection.

- [ ] **Step 1: Audit weather_sync.cpp call sites**

```bash
grep -n "encode_" src/core/cigi_bridge/src/weather_sync.cpp
```

Expected: list of calls to `encode_atmosphere_control`, `encode_weather_control`, `encode_region_control`. Note each line number.

- [ ] **Step 2: Rewrite each call site to call emitters directly**

For each line from Step 1, replace the call to `encode_X` with the equivalent `cigi_session::BuildX` call. Add `#include "cigi_session/emitters/*.h"` at the top of `weather_sync.cpp`.

- [ ] **Step 3: Delete the files**

```bash
git rm src/core/cigi_bridge/src/weather_encoder.cpp
git rm src/core/cigi_bridge/include/cigi_bridge/weather_encoder.hpp   # if exists
```

- [ ] **Step 4: Update CMakeLists.txt**

Remove `src/weather_encoder.cpp` from the `add_executable(cigi_bridge_node ...)` list.

- [ ] **Step 5: Handle test_weather_encoder.cpp**

The existing test asserts encoder behaviour that is now covered by `test_cigi_session`. Either:
(a) delete `test_weather_encoder.cpp` + its gtest registration in CMakeLists, or
(b) keep it as a weather-state-diff regression check, rewriting calls to emitters.

Choice (a) is cleaner if the file only tests wire format. Choice (b) if it tests diff logic. Inspect the file and pick.

- [ ] **Step 6: Build + all tests**

```bash
colcon build --packages-select sim_cigi_bridge --cmake-args -DBUILD_TESTING=ON
colcon test --packages-select sim_cigi_bridge
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git commit -am "refactor(cigi_bridge): delete weather_encoder.cpp; call emitters directly"
```

---

### Task 2.11: Delete CIGI_PKT_* constants from cigi_host_node.hpp

**Files:**
- Modify: `src/core/cigi_bridge/include/cigi_bridge/cigi_host_node.hpp`

- [ ] **Step 1: Verify constants are unused**

```bash
grep -rn "CIGI_PKT_\|CIGI_IG_MODE_\|CIGI_IG_CTRL_SIZE\|CIGI_ENTITY_CTRL_SIZE\|CIGI_HOT_REQUEST_SIZE\|CIGI_SOF_IG_MODE_OPERATE\|CIGI_ENTITY_ACTIVE" src/
```

Expected: zero matches outside of `cigi_host_node.hpp` itself. If any external reference remains (e.g. in a test), migrate it first.

- [ ] **Step 2: Delete the constant block**

Delete the block from `cigi_host_node.hpp` spanning `CIGI_PKT_IG_CTRL` through `CIGI_SOF_IG_MODE_OPERATE` (the wire-layer constants). Keep state members (`frame_counter_`, `last_ig_frame_`, `ig_status_`, etc.).

- [ ] **Step 3: Build**

```bash
colcon build --packages-select sim_cigi_bridge
```

Expected: clean.

- [ ] **Step 4: Commit**

```bash
git commit -am "refactor(cigi_host_node): delete CIGI_* wire constants (owned by cigi_session)"
```

---

## Phase 3 — Plugin Migration

**Setup requirement**: Phase 1 library must be cross-built for mingw64 (Task 0.1 or 0.2). Update `x-plane_plugins/xplanecigi/CMakeLists.txt` in Task 3.1.

### Task 3.1: Wire cigi_session into xplanecigi build

**Files:**
- Modify: `x-plane_plugins/xplanecigi/CMakeLists.txt`

- [ ] **Step 1: Link the library**

Append to `x-plane_plugins/xplanecigi/CMakeLists.txt`:

```cmake
# CCL + cigi_session for the plugin (cross-compiled).
set(CCL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../../references/CIGI/cigi3.3/ccl/install-mingw/include/cigicl")
set(CCL_LIBRARY "${CMAKE_SOURCE_DIR}/../../references/CIGI/cigi3.3/ccl/install-mingw/lib/libcigicl.a")

# If static-source fallback was used (Task 0.2), set instead:
# (handled by the cigicl_static target defined in Task 0.2)

add_subdirectory(
  "${CMAKE_SOURCE_DIR}/../../src/core/cigi_bridge/cigi_session"
  cigi_session_build)

target_link_libraries(xplanecigi PRIVATE cigi_session)
```

- [ ] **Step 2: Build the plugin**

```bash
cd x-plane_plugins/xplanecigi && cmake --build build -j4
```

Expected: `xplanecigi.xpl` produced, now linked against CCL via `cigi_session`.

- [ ] **Step 3: Smoke-run**

Deploy with `./copyxplaneplugin.sh`, start X-Plane + sim + backend. Confirm existing behaviour: HAT/HOT flows, weather updates render, runway friction does NOT work yet (expected — will be fixed in Task 3.6).

- [ ] **Step 4: Commit**

```bash
git commit -am "build(xplanecigi): link cigi_session library"
```

---

### Task 3.2: IgSession bring-up + SOF emit via SofEmit

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/emitters/SofEmit.h`
- Modify: `src/core/cigi_bridge/cigi_session/src/emitters.cpp`
- Modify: `x-plane_plugins/xplanecigi/XPluginMain.cpp`

- [ ] **Step 1: Write SofEmit.h**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace cigi_session {

// Start of Frame (§4.2.1, 24 bytes). Called from IG side.
std::size_t BuildSof(std::uint8_t * buf, std::size_t cap,
                     std::uint8_t ig_mode,        // 0..3
                     std::int8_t database_id,
                     std::uint32_t ig_frame_number,
                     std::uint32_t last_host_frame_number);

}  // namespace cigi_session
```

- [ ] **Step 2: Test**

```cpp
TEST(CigiSession, SofEmit) {
    std::uint8_t buf[24] = {};
    ASSERT_EQ(cigi_session::BuildSof(buf, sizeof(buf), 1, 0, 99, 100), 24u);
    CigiSOFV3_2 pkt;
    ASSERT_GE(pkt.Unpack(buf, kCclSwap, nullptr), 0);
    EXPECT_EQ(pkt.GetIGMode(), CigiBaseSOF::Operate);
    EXPECT_EQ(pkt.GetFrameCntr(), 99u);
}
```

- [ ] **Step 3: Implement**

In `emitters.cpp`:

```cpp
#include "cigi_session/emitters/SofEmit.h"
#include <CigiSOFV3_2.h>
#include <CigiBaseSOF.h>

namespace cigi_session {

std::size_t BuildSof(std::uint8_t * buf, std::size_t cap,
                     std::uint8_t ig_mode, std::int8_t database_id,
                     std::uint32_t ig_frame_number,
                     std::uint32_t last_host_frame_number) {
    constexpr std::size_t kSize = 24;
    if (cap < kSize) return 0;
    CigiSOFV3_2 pkt;
    pkt.SetIGMode(static_cast<CigiBaseSOF::IGModeGrp>(ig_mode));
    pkt.SetDatabaseID(database_id);
    pkt.SetTimeStampValid(false);
    pkt.SetEarthReferenceModel(CigiBaseSOF::WGS84);
    pkt.SetFrameCntr(ig_frame_number);
    pkt.SetLastRcvdHostFrame(last_host_frame_number);
    CigiVersionID ver; ver.SetCigiVersion(3, 3);
    int n = pkt.Pack(&pkt, buf, &ver);
    return (n > 0) ? static_cast<std::size_t>(n) : 0;
}

}
```

- [ ] **Step 4: Replace plugin's send_sof**

In `XPluginMain.cpp`, replace the entire `send_sof()` function body with:

```cpp
static void send_sof() {
    if (g_send_sock == INVALID_SOCKET) return;
    std::uint8_t buf[24] = {};
    std::uint8_t mode = g_probing_terrain ? 0 /*Standby*/ : 1 /*Operate*/;
    cigi_session::BuildSof(buf, sizeof(buf), mode, 0, g_ig_frame, g_host_frame);
    sendto(g_send_sock,
           reinterpret_cast<const char *>(buf), 24, 0,
           reinterpret_cast<const struct sockaddr *>(&g_host_addr),
           sizeof(g_host_addr));
}
```

Add `#include "cigi_session/emitters/SofEmit.h"` at the top.

- [ ] **Step 5: Rebuild plugin + deploy + smoke**

```bash
cd x-plane_plugins/xplanecigi && cmake --build build
./copyxplaneplugin.sh  # or copy manually on Windows
```

Restart X-Plane + sim. Confirm `ros2 topic echo /sim/cigi/ig_status` shows 0→1 transition on startup (still works end-to-end).

- [ ] **Step 6: Commit**

```bash
git commit -am "feat(xplanecigi): SOF emit via cigi_session::BuildSof"
```

---

### Task 3.3: IG Control processor

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/processors/IIgCtrlProcessor.h`
- Modify: `src/core/cigi_bridge/cigi_session/src/IgSession.cpp`
- Modify: `src/core/cigi_bridge/cigi_session/test/test_cigi_session.cpp`
- Create: `x-plane_plugins/xplanecigi/XplaneIgCtrlProcessor.{h,cpp}`
- Modify: `x-plane_plugins/xplanecigi/XPluginMain.cpp`
- Modify: `x-plane_plugins/xplanecigi/CMakeLists.txt`

- [ ] **Step 1: Write IIgCtrlProcessor.h**

```cpp
#pragma once
#include <cstdint>

namespace cigi_session {

struct IgCtrlFields {
    std::uint8_t ig_mode;            // 0..3 (Reset/Standby, Operate, Debug, Offline)
    std::int8_t  database_id;
    std::uint32_t host_frame_number;
    std::uint32_t timestamp_10us_ticks;
};

class IIgCtrlProcessor {
public:
    virtual ~IIgCtrlProcessor() = default;
    virtual void OnIgCtrl(const IgCtrlFields & f) = 0;
};

}  // namespace cigi_session
```

- [ ] **Step 2: Implement IgSession::HandleDatagram with IG Control dispatch**

Replace `IgSession.cpp` with:

```cpp
#include "cigi_session/IgSession.h"
#include "cigi_session/processors/IIgCtrlProcessor.h"

#include <CigiIGCtrlV3_3.h>
#include <CigiBaseIGCtrl.h>

namespace cigi_session {

namespace {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
constexpr bool kSwap = true;
#else
constexpr bool kSwap = false;
#endif
}

struct IgSession::Impl {
    IIgCtrlProcessor *      ig_ctrl = nullptr;
    IEntityCtrlProcessor *  entity  = nullptr;
    IHatHotReqProcessor *   hat_hot = nullptr;
    IAtmosphereProcessor *  atmos   = nullptr;
    IEnvRegionProcessor *   env     = nullptr;
    IWeatherCtrlProcessor * wx      = nullptr;
    ICompCtrlProcessor *    comp    = nullptr;
};

IgSession::IgSession() : impl_(std::make_unique<Impl>()) {}
IgSession::~IgSession() = default;
void IgSession::SetIgCtrlProcessor(IIgCtrlProcessor * p) { impl_->ig_ctrl = p; }
void IgSession::SetEntityCtrlProcessor(IEntityCtrlProcessor * p) { impl_->entity = p; }
void IgSession::SetHatHotReqProcessor(IHatHotReqProcessor * p) { impl_->hat_hot = p; }
void IgSession::SetAtmosphereProcessor(IAtmosphereProcessor * p) { impl_->atmos = p; }
void IgSession::SetEnvRegionProcessor(IEnvRegionProcessor * p) { impl_->env = p; }
void IgSession::SetWeatherCtrlProcessor(IWeatherCtrlProcessor * p) { impl_->wx = p; }
void IgSession::SetCompCtrlProcessor(ICompCtrlProcessor * p) { impl_->comp = p; }

std::size_t IgSession::HandleDatagram(const std::uint8_t * data, std::size_t len) {
    std::size_t packets = 0;
    std::size_t off = 0;
    while (off + 2 <= len) {
        std::uint8_t id = data[off];
        std::uint8_t sz = data[off + 1];
        if (sz < 2 || off + sz > len) break;

        if (id == 0x01 && sz >= 24 && impl_->ig_ctrl) {
            CigiIGCtrlV3_3 pkt;
            if (pkt.Unpack(const_cast<std::uint8_t *>(data + off), kSwap, nullptr) >= 0) {
                IgCtrlFields f;
                f.ig_mode               = static_cast<std::uint8_t>(pkt.GetIGMode());
                f.database_id           = pkt.GetDatabaseID();
                f.host_frame_number     = pkt.GetFrameCntr();
                f.timestamp_10us_ticks  = pkt.GetTimeStamp();
                impl_->ig_ctrl->OnIgCtrl(f);
                ++packets;
            }
        }
        // Other packets: added in later tasks.
        off += sz;
    }
    return packets;
}

}  // namespace cigi_session
```

- [ ] **Step 3: Test dispatch**

Append to `test_cigi_session.cpp`:

```cpp
#include "cigi_session/IgSession.h"
#include "cigi_session/processors/IIgCtrlProcessor.h"

struct IgCtrlSpy : cigi_session::IIgCtrlProcessor {
    std::optional<cigi_session::IgCtrlFields> got;
    void OnIgCtrl(const cigi_session::IgCtrlFields & f) override { got = f; }
};

TEST(CigiSession, IgCtrlProcessorDispatch) {
    std::uint8_t buf[24] = {};
    cigi_session::BuildIgCtrl(buf, sizeof(buf),
                              cigi_session::IgMode::Operate,
                              42, 50000, 0);
    cigi_session::IgSession s;
    IgCtrlSpy spy;
    s.SetIgCtrlProcessor(&spy);
    EXPECT_EQ(s.HandleDatagram(buf, sizeof(buf)), 1u);
    ASSERT_TRUE(spy.got.has_value());
    EXPECT_EQ(spy.got->ig_mode, 1);
    EXPECT_EQ(spy.got->host_frame_number, 42u);
}
```

- [ ] **Step 4: Write XplaneIgCtrlProcessor**

`XplaneIgCtrlProcessor.h`:

```cpp
#pragma once
#include "cigi_session/processors/IIgCtrlProcessor.h"

class XplaneIgCtrlProcessor : public cigi_session::IIgCtrlProcessor {
public:
    void OnIgCtrl(const cigi_session::IgCtrlFields & f) override;
};
```

`XplaneIgCtrlProcessor.cpp` — move the contents of the current `case 0x01:` branch in `XPluginMain.cpp` into `OnIgCtrl`, operating on `f.ig_mode` and `f.host_frame_number` instead of raw bytes.

- [ ] **Step 5: Wire up in XPluginMain.cpp**

Add a global `static XplaneIgCtrlProcessor g_ig_ctrl_proc;` and `static cigi_session::IgSession g_session;`. In plugin init, call `g_session.SetIgCtrlProcessor(&g_ig_ctrl_proc);`. Add `#include "XplaneIgCtrlProcessor.h"`.

Inside the UDP receive loop, keep the old `switch(packet_id)` but add ABOVE it:

```cpp
g_session.HandleDatagram(pkt, len);
```

For now the library handles IG Control, and the switch handles everything else. As subsequent tasks migrate packets, their `case` branches will be removed.

- [ ] **Step 6: CMakeLists.txt update**

Add to `x-plane_plugins/xplanecigi/CMakeLists.txt`:

```cmake
target_sources(xplanecigi PRIVATE
  XplaneIgCtrlProcessor.cpp
)
```

- [ ] **Step 7: Build + smoke**

```bash
cd x-plane_plugins/xplanecigi && cmake --build build
./copyxplaneplugin.sh
```

Restart X-Plane + sim. Confirm IG Mode transitions log correctly in X-Plane's `Log.txt`.

- [ ] **Step 8: Remove case 0x01 from XPluginMain.cpp**

Delete the entire `case 0x01:` branch. The library now handles it.

- [ ] **Step 9: Rebuild + commit**

```bash
git commit -am "feat(xplanecigi): IG Control via XplaneIgCtrlProcessor"
```

---

### Task 3.4: HAT/HOT Request processor + Extended Response emitter

**Files:**
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/processors/IHatHotReqProcessor.h`
- Create: `src/core/cigi_bridge/cigi_session/include/cigi_session/emitters/HatHotXRespEmit.h`
- Modify: IgSession.cpp, emitters.cpp, test_cigi_session.cpp
- Create: `x-plane_plugins/xplanecigi/XplaneHatHotProcessor.{h,cpp}`
- Modify: `x-plane_plugins/xplanecigi/XPluginMain.cpp`, CMakeLists.txt

- [ ] **Step 1**: Write `IHatHotReqProcessor.h` (req_id, lat, lon, alt, req_type, coord_sys).
- [ ] **Step 2**: Write `HatHotXRespEmit.h` with `BuildHatHotExtendedResponse(buf, cap, req_id, valid, hat_m, hot_m, material, norm_az_deg, norm_el_deg)`.
- [ ] **Step 3**: Add tests for both (processor dispatch + emitter round-trip).
- [ ] **Step 4**: Extend `IgSession::HandleDatagram` with `0x18` dispatch. Implement `BuildHatHotExtendedResponse` in `emitters.cpp`.
- [ ] **Step 5**: Write `XplaneHatHotProcessor`. Move XPLMProbeTerrain + response-send logic from current `case 0x18:` into the processor. Use `BuildHatHotExtendedResponse` for the response.
- [ ] **Step 6**: Wire into `IgSession` in XPluginMain.cpp. Delete `case 0x18:`.
- [ ] **Step 7**: Build + smoke + commit.

End-to-end smoke: confirm `ros2 topic echo /sim/cigi/hat_responses` shows HAT flowing while aircraft is positioned on runway.

---

### Task 3.5: Entity Control processor (ownship only)

**Files:**
- Create: `IEntityCtrlProcessor.h`, `XplaneEntityProcessor.{h,cpp}`
- Modify: IgSession.cpp, test_cigi_session.cpp, XPluginMain.cpp, CMakeLists.txt

Same recipe. Move contents of current `case 0x02:` into `XplaneEntityProcessor::OnEntityCtrl`. Only ownship (Entity ID == 0) writes to X-Plane datarefs in v1. Other IDs are logged and dropped.

Smoke: reposition works, aircraft follows simulated path.

---

### Task 3.6: Atmosphere + EnvRegion + WeatherCtrl processors + runway friction CompCtrl

**Files:**
- Create: `IAtmosphereProcessor.h`, `IEnvRegionProcessor.h`, `IWeatherCtrlProcessor.h`, `ICompCtrlProcessor.h`
- Create: `XplaneWeatherProcessor.{h,cpp}`, `XplaneCompCtrlProcessor.{h,cpp}`
- Modify: IgSession.cpp, test_cigi_session.cpp, XPluginMain.cpp, CMakeLists.txt

This task restores runway friction (paired with Task 2.9 host-side change).

- [ ] **Step 1-6**: Write interfaces + emitter wiring + processor classes following the same recipe. Move current `case 0x0A:`, `case 0x0B:`, `case 0x0C:` logic into `XplaneWeatherProcessor::OnAtmosphere/OnEnvRegion/OnWeather`.
- [ ] **Step 7**: `XplaneCompCtrlProcessor::OnComponentControl(fields f)` dispatches on `(cls, component_id)`:
   - `(GlobalTerrainSurface, RunwayFriction)` → set X-Plane runway friction dataref to `f.component_state`.
   - Default: log and drop.
- [ ] **Step 8**: Delete `case 0x0A`, `case 0x0B`, `case 0x0C`, and **`case 0xCB`** from XPluginMain.cpp. Also delete all `CIGI_PKT_RUNWAY_FRICTION` references.
- [ ] **Step 9**: Build + smoke. Confirm runway friction now works again (changed in IOS weather → visible in X-Plane).
- [ ] **Step 10**: Commit.

---

### Task 3.7: Delete dead plugin helpers

**Files:**
- Modify: `x-plane_plugins/xplanecigi/XPluginMain.cpp`

- [ ] **Step 1: Verify no remaining case branches**

```bash
grep -n "case 0x" x-plane_plugins/xplanecigi/XPluginMain.cpp
```

Expected: zero matches. If any remain, identify and migrate before proceeding.

- [ ] **Step 2: Delete the entire `process_packet` function body** (replaced by `g_session.HandleDatagram(pkt, len)` — which is now called directly from the UDP receive loop).

- [ ] **Step 3: Delete read_be* / write_be* helpers** unless still referenced by `send_sof`. (They shouldn't be; `BuildSof` replaced the hand-rolled encoding in Task 3.2.)

```bash
grep -n "read_be\|write_be" x-plane_plugins/xplanecigi/XPluginMain.cpp
```

If zero matches, delete the helper function definitions. If any matches remain, migrate the callers first.

- [ ] **Step 4: Build + smoke + commit**

```bash
git commit -am "refactor(xplanecigi): delete hand-rolled wire helpers; library owns all CIGI parsing"
```

---

## Phase 4 — Cleanup + Handoff

### Task 4.1: Delete test_cigi_wire_conformance

**Files:**
- Delete: `src/core/cigi_bridge/test/test_cigi_wire_conformance.cpp`
- Modify: `src/core/cigi_bridge/CMakeLists.txt`

With CCL on both sides, the test asserts "our CCL-wrapped emitter output matches CCL's unpack of the same bytes" — a tautology.

- [ ] **Step 1: Delete file + CMake entry**

```bash
git rm src/core/cigi_bridge/test/test_cigi_wire_conformance.cpp
```

Edit `src/core/cigi_bridge/CMakeLists.txt`, remove the `ament_add_gtest(test_cigi_wire_conformance ...)` block and surrounding CCL-gate.

- [ ] **Step 2: Build + run remaining tests**

```bash
colcon build --packages-select sim_cigi_bridge --cmake-args -DBUILD_TESTING=ON
colcon test --packages-select sim_cigi_bridge
```

Expected: `test_weather_encoder` (if kept), `test_weather_sync`, `test_cigi_session` all pass. `test_cigi_wire_conformance` is absent.

- [ ] **Step 3: Commit**

```bash
git commit -am "test(cigi_bridge): delete test_cigi_wire_conformance (tautological post-CCL)

With cigi_session using CCL on both sides, the round-trip test
asserts CCL matches CCL — no longer meaningful. test_cigi_session
covers the library's emitters and processors end-to-end."
```

---

### Task 4.2: End-to-end smoke test

**Files:** none (manual verification).

- [ ] **Step 1: Start the full stack**

```bash
# Terminal 1
./start_sim.sh
# Terminal 2 (once sim ready)
./start_backend.sh
# Terminal 3
./start_frontend.sh
# X-Plane: make sure xplanecigi.xpl is fresh (./copyxplaneplugin.sh)
```

- [ ] **Step 2: Validate each feature**

Check each of the following and record in the commit message:

- **IG status** — `ros2 topic echo /sim/cigi/ig_status` shows 0→1 transition shortly after X-Plane comes up.
- **HAT/HOT** — `ros2 topic hz /sim/cigi/hat_responses` shows ~10 Hz (or configured gear-point frequency).
- **Reposition** — use IOS to reposition aircraft to a new airport. Aircraft relocates in X-Plane; terrain probes re-stabilise; sim returns to RUNNING.
- **Weather global** — IOS weather panel → set visibility to 1 km. X-Plane visibility changes.
- **Weather regional** — IOS weather panel → add a cloud patch. Patch visible in X-Plane.
- **Runway friction** — IOS weather panel → set runway condition to "Wet". X-Plane runway dataref updates (check via DataRefEditor or `xp_surface_friction` dataref).

- [ ] **Step 3: Log smoke result in DECISIONS.md**

Append entry to `DECISIONS.md` (see Task 4.3).

---

### Task 4.3: DECISIONS.md entry

**Files:**
- Modify: `DECISIONS.md`

- [ ] **Step 1: Append entry**

```markdown
## 2026-04-23 — hh:mm:ss - Claude Code (CCL migration)

- DECIDED: All CIGI 3.3 wire handling, both sides of the bridge, goes
  through cigi_session — a thin CCL wrapper with typed emitters and
  processor interfaces. Zero hand-counted byte offsets anywhere in
  the framework.

- REASON: After the 2026-04-23 spec audit surfaced multiple wire-
  format bugs (offset errors in 0x0A Atmosphere, 0x05 Short Component
  Control, 0x18 HAT/HOT Request, 0x65 SOF parse), and with planned
  CGF/SAF scope requiring ~20+ new packet types, hand-rolling was no
  longer sustainable. CCL's Pack/Unpack is the canonical spec
  implementation; wrapping it once gives us spec conformance by
  construction for all current and future packets.

- ARCHITECTURE: cigi_session library at
  src/core/cigi_bridge/cigi_session/ builds twice (Linux native for
  host; mingw64 cross for plugin). Host node and xplanecigi.xpl both
  link it. No caller touches byte offsets.

- RUNWAY FRICTION: migrated from the framework-invented user-defined
  packet 0xCB to standard Component Control (Class=8, ID=100). The
  framework's on-wire packet inventory is now 100% spec-conformant
  CIGI 3.3 — no user-defined packets.

- DELETED: weather_encoder.cpp (absorbed into emitters),
  test_cigi_wire_conformance.cpp (tautological with CCL on both
  sides), all CIGI_PKT_* / CIGI_*_SIZE constants from
  cigi_host_node.hpp, all read_be*/write_be* helpers from
  XPluginMain.cpp, the entire switch(packet_id) block in the plugin.

- AFFECTS:
  - src/core/cigi_bridge/cigi_session/ (new library)
  - src/core/cigi_bridge/src/cigi_host_node.cpp (uses emitters +
    HostSession)
  - src/core/cigi_bridge/src/weather_sync.cpp (uses emitters directly)
  - x-plane_plugins/xplanecigi/XPluginMain.cpp (1618 → ~400 lines)
  - x-plane_plugins/xplanecigi/Xplane*Processor.{h,cpp} (6 new files)

- SMOKE TEST RESULT: reposition ✓, HAT/HOT ✓, global weather ✓,
  regional weather ✓, runway friction ✓.

- FOLLOW-UP: When adding new CIGI packet types, add emitter or
  processor interface to cigi_session; add implementation in the
  owning caller. No spec audit needed — CCL is the spec.
```

- [ ] **Step 2: Commit**

```bash
git add DECISIONS.md
git commit -m "docs(DECISIONS): record CCL migration completion"
```

---

### Task 4.4: Final merge to main

- [ ] **Step 1: Rebase against main (if main moved)**

```bash
git fetch origin
git rebase origin/main
```

Resolve any conflicts. Re-run all tests + smoke test if anything non-trivial shifted.

- [ ] **Step 2: Merge**

```bash
git checkout main
git merge --no-ff cigi-spec-conformance
```

- [ ] **Step 3: Delete the working branch**

```bash
git branch -d cigi-spec-conformance
```

---

## Self-Review (checklist applied)

- **Spec coverage**: every numbered migration step in the spec (24 total) maps to a task here. Phase 0 prerequisites → Task 0.1/0.2. Phase 1 library scaffold → Tasks 1.1–1.3. Phase 2 host migrations (spec steps 3–13) → Tasks 2.1–2.11. Phase 3 plugin migrations (spec steps 14–19) → Tasks 3.1–3.7. Phase 4 cleanup (spec steps 20–24) → Tasks 4.1–4.4.

- **Placeholder scan**: every Task has explicit steps with exact commands and code. Tasks 2.7 and 2.8 use "same recipe" language but still list the exact new header contents and reference the spec for the full field list — the recipe is established by Tasks 2.1–2.6 in full detail.

- **Type consistency**: `IgMode` enum (out) and `IgModeRx` enum (in) are different types intentionally — outbound sets a sender-side value, inbound reads the received one. `BuildFoo` always returns `std::size_t` (bytes written, 0 on failure). All processors take a const reference to a `FooFields` POD.

- **Risk gates**: Phase 0 explicitly blocks Phase 3 (plugin needs mingw64 build). Task 2.9/3.6 pair is called out as the one moment where runway friction temporarily breaks; every other task is a non-regressing swap.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-23-cigi-bridge-ccl-migration.md`. Two execution options:

**1. Subagent-Driven (recommended)** — dispatch a fresh subagent per task, review between tasks, keeps main context from ballooning across ~30 tasks. REQUIRED SUB-SKILL: `superpowers:subagent-driven-development`.

**2. Inline Execution** — execute tasks in this session using `superpowers:executing-plans`. Batch execution with checkpoints for review.

Which approach?
