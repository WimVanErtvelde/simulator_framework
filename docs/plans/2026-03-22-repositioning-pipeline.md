# Repositioning Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement sequenced repositioning with CIGI terrain readiness, so the instructor clicks SET POSITION, sees "REPOSITIONING" for 1-3 seconds while terrain resolves, then aircraft appears at the correct ground height.

**Architecture:** New REPOSITIONING state in sim_manager orchestrates the sequence. X-Plane plugin signals terrain readiness via CIGI SOF IG Status. flight_model_adapter waits for CIGI HOT (or SRTM fallback), computes CG height from gear geometry, publishes terrain ready. sim_manager exits REPOSITIONING on ready signal or timeout.

**Tech Stack:** C++ (ROS2 nodes, X-Plane plugin), React/JS (IOS frontend), CIGI 3.3 protocol

**Spec:** `docs/specs/2026-03-22-repositioning-pipeline-design.md`

---

### Task 1: Add STATE_REPOSITIONING to SimState.msg

**Files:**
- Modify: `src/sim_msgs/msg/SimState.msg`

- [ ] **Step 1: Add the new state constant**

In `src/sim_msgs/msg/SimState.msg`, add after `STATE_SHUTDOWN = 5`:
```
uint8 STATE_REPOSITIONING = 6
```

- [ ] **Step 2: Build sim_msgs**

```bash
cd ~/simulator_framework
source /opt/ros/jazzy/setup.bash && colcon build --packages-select sim_msgs && source install/setup.bash
```
Expected: builds clean.

- [ ] **Step 3: Commit**

```bash
git add src/sim_msgs/msg/SimState.msg
git commit -m "msg: add STATE_REPOSITIONING = 6 to SimState"
```

---

### Task 2: sim_manager — REPOSITIONING state + terrain ready subscription

**Files:**
- Modify: `src/core/sim_manager/src/sim_manager_node.cpp`

**Context:** The sim_manager owns the state machine. Read the existing `transition_to()` function (around line 296), `on_command()` (around line 348), `check_node_health()` (around line 594), and `on_clock_tick()` (around line 258) to understand current patterns.

- [ ] **Step 1: Add REPOSITIONING to state_name()**

In the `state_name()` switch (around line 283), add:
```cpp
case sim_msgs::msg::SimState::STATE_REPOSITIONING: return "REPOSITIONING";
```

- [ ] **Step 2: Add valid transitions for REPOSITIONING**

In the `transition_to()` switch (around line 304), add REPOSITIONING as a valid target from READY, RUNNING, and FROZEN:
```cpp
case S::STATE_READY:
    valid = (new_state == S::STATE_RUNNING || new_state == S::STATE_REPOSITIONING ||
             new_state == S::STATE_SHUTDOWN);
    break;
case S::STATE_RUNNING:
    valid = (new_state == S::STATE_FROZEN || new_state == S::STATE_RESETTING ||
             new_state == S::STATE_REPOSITIONING || new_state == S::STATE_SHUTDOWN);
    break;
case S::STATE_FROZEN:
    valid = (new_state == S::STATE_RUNNING || new_state == S::STATE_RESETTING ||
             new_state == S::STATE_REPOSITIONING || new_state == S::STATE_SHUTDOWN);
    break;
```

Add REPOSITIONING → READY transition:
```cpp
case S::STATE_REPOSITIONING:
    valid = (new_state == S::STATE_READY || new_state == S::STATE_SHUTDOWN);
    break;
```

- [ ] **Step 3: Subscribe to /sim/terrain/ready**

In the constructor or on_configure, add a subscription:
```cpp
terrain_ready_sub_ = this->create_subscription<std_msgs::msg::Bool>(
    "/sim/terrain/ready", 10,
    [this](const std_msgs::msg::Bool::SharedPtr msg) {
        if (msg->data && state_ == sim_msgs::msg::SimState::STATE_REPOSITIONING) {
            RCLCPP_INFO(this->get_logger(), "Terrain ready — exiting REPOSITIONING");
            transition_to(sim_msgs::msg::SimState::STATE_READY);
        }
    });
```

Add include: `#include <std_msgs/msg/bool.hpp>`
Add member: `rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr terrain_ready_sub_;`
Clean up in `on_cleanup()`: `terrain_ready_sub_.reset();`

- [ ] **Step 4: Transition to REPOSITIONING on IC received**

In the IC subscription callback (search for `/sim/initial_conditions`), add at the start:
```cpp
if (state_ != sim_msgs::msg::SimState::STATE_SHUTDOWN &&
    state_ != sim_msgs::msg::SimState::STATE_INIT) {
    transition_to(sim_msgs::msg::SimState::STATE_REPOSITIONING);
    repositioning_start_ = std::chrono::steady_clock::now();
}
```

Add member: `std::chrono::steady_clock::time_point repositioning_start_{};`

- [ ] **Step 5: Add timeout in check_node_health or a dedicated timer**

In the `check_node_health()` function (2Hz), add at the end:
```cpp
// Repositioning timeout
if (state_ == sim_msgs::msg::SimState::STATE_REPOSITIONING) {
    auto age = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - repositioning_start_).count();
    if (age > 5.0) {
        RCLCPP_WARN(this->get_logger(),
            "Repositioning timeout (%.1fs) — transitioning to READY", age);
        transition_to(sim_msgs::msg::SimState::STATE_READY);
    }
}
```

- [ ] **Step 6: Pause clock during REPOSITIONING**

In `on_clock_tick()`, the clock already only advances in RUNNING state (line 265: `if (state_ == S::STATE_RUNNING)`). REPOSITIONING is implicitly paused. No change needed — verify this.

- [ ] **Step 7: Build and test**

```bash
colcon build --packages-select sim_manager && source install/setup.bash
```
Expected: builds clean.

- [ ] **Step 8: Commit**

```bash
git add src/core/sim_manager/src/sim_manager_node.cpp
git commit -m "sim_manager: add REPOSITIONING state with terrain ready subscription and timeout"
```

---

### Task 3: flight_model_adapter — terrain ready publisher + gear height from config

**Files:**
- Modify: `src/core/flight_model_adapter/src/flight_model_adapter_node.cpp`

**Context:** This node handles ICs, CIGI HOT responses, and SRTM fallback. Read the IC subscription (around line 104), `apply_ic_with_terrain()` (around line 431), and the HOT response subscription (around line 74). Also read `src/aircraft/c172/config/config.yaml` for gear_points structure.

- [ ] **Step 1: Add terrain ready publisher**

Add include: `#include <std_msgs/msg/bool.hpp>`

In on_configure, create publisher:
```cpp
terrain_ready_pub_ = this->create_publisher<std_msgs::msg::Bool>(
    "/sim/terrain/ready", 10);
```

Add member: `rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr terrain_ready_pub_;`
Clean up in cleanup: `terrain_ready_pub_.reset();`

Add helper:
```cpp
void publish_terrain_ready(bool ready) {
    auto msg = std_msgs::msg::Bool();
    msg.data = ready;
    terrain_ready_pub_->publish(msg);
}
```

- [ ] **Step 2: Load gear_cg_height from aircraft config.yaml**

Add includes: `#include <yaml-cpp/yaml.h>`, `#include <ament_index_cpp/get_package_share_directory.hpp>`

In on_configure, after adapter is created, load gear height:
```cpp
// Load gear CG height from aircraft config
try {
    auto aircraft_id = this->get_parameter("aircraft_id").as_string();
    auto config_path = ament_index_cpp::get_package_share_directory("aircraft_" + aircraft_id)
        + "/config/config.yaml";
    YAML::Node config = YAML::LoadFile(config_path);
    auto gear_points = config["gear_points"];
    if (gear_points && gear_points.IsSequence()) {
        double max_z = 0.0;
        for (const auto & gp : gear_points) {
            double z = std::abs(gp["z_m"].as<double>(0.0));
            if (z > max_z) max_z = z;
        }
        gear_cg_height_m_ = max_z;
        RCLCPP_INFO(this->get_logger(), "Gear CG height from config: %.2f m", gear_cg_height_m_);
    }
} catch (const std::exception & e) {
    RCLCPP_WARN(this->get_logger(), "Could not load gear height from config: %s — using default %.2fm",
        e.what(), gear_cg_height_m_);
}
```

Add member: `double gear_cg_height_m_ = 0.5;  // default fallback`

Add to CMakeLists.txt: `find_package(yaml-cpp REQUIRED)` and `target_link_libraries` for yaml-cpp and ament_index_cpp.

- [ ] **Step 3: Simplify IC handler — apply raw IC, wait for terrain**

Replace the entire IC subscription callback with:
```cpp
[this](const sim_msgs::msg::InitialConditions::SharedPtr msg) {
    if (!adapter_) return;
    RCLCPP_INFO(this->get_logger(), "Applying initial conditions: config=%s",
                msg->configuration.c_str());

    // Clear stale terrain data
    terrain_hot_.clear();
    ic_cigi_refined_ = false;
    ic_srtm_applied_ = false;
    srtm_valid_ = false;
    publish_terrain_ready(false);

    // Apply raw IC — entity moves to target lat/lon
    adapter_->apply_initial_conditions(*msg);

    // Save IC for terrain refinement
    pending_ic_ = std::make_shared<sim_msgs::msg::InitialConditions>(*msg);
    pending_ic_time_ = std::chrono::steady_clock::now();

    // Query SRTM as fallback (async)
    if (terrain_client_ && terrain_client_->service_is_ready()) {
        auto req = std::make_shared<sim_msgs::srv::GetTerrainElevation::Request>();
        req->latitude_rad = msg->latitude_rad;
        req->longitude_rad = msg->longitude_rad;
        terrain_client_->async_send_request(req,
            [this](rclcpp::Client<sim_msgs::srv::GetTerrainElevation>::SharedFuture future) {
                auto resp = future.get();
                if (resp && resp->valid) {
                    srtm_terrain_m_ = resp->elevation_msl_m;
                    srtm_valid_ = true;
                }
            });
    }
}
```

- [ ] **Step 4: Update terrain refinement in update loop**

Replace the existing IC terrain refinement block with:
```cpp
if (pending_ic_) {
    auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - pending_ic_time_).count();

    // CIGI HOT: apply when all gear point responses received (>200ms)
    if (!ic_cigi_refined_ && !terrain_hot_.empty() && age_ms >= 200) {
        double sum = 0.0;
        for (auto & [name, hot] : terrain_hot_) sum += hot;
        double terrain_m = sum / terrain_hot_.size();
        RCLCPP_INFO(this->get_logger(),
            "IC terrain from CIGI HOT: %.1f m MSL (after %ldms)", terrain_m, age_ms);
        apply_ic_with_terrain(*pending_ic_, terrain_m);
        ic_cigi_refined_ = true;
        publish_terrain_ready(true);
        pending_ic_.reset();
    }

    // SRTM fallback: apply after 2s if no CIGI
    if (!ic_cigi_refined_ && !ic_srtm_applied_ && srtm_valid_ && age_ms >= 2000) {
        RCLCPP_INFO(this->get_logger(),
            "IC terrain from SRTM (no CIGI): %.1f m MSL", srtm_terrain_m_);
        apply_ic_with_terrain(*pending_ic_, srtm_terrain_m_);
        ic_srtm_applied_ = true;
        publish_terrain_ready(true);
        pending_ic_.reset();
    }

    // Hard timeout: 5s — give up
    if (age_ms > 5000 && !ic_cigi_refined_ && !ic_srtm_applied_) {
        RCLCPP_WARN(this->get_logger(), "IC terrain timeout — using raw altitude");
        publish_terrain_ready(true);
        pending_ic_.reset();
    }
}
```

- [ ] **Step 5: Fix apply_ic_with_terrain — set terrain AFTER RunIC, use gear height**

```cpp
void apply_ic_with_terrain(const sim_msgs::msg::InitialConditions & ic, double terrain_elev_m)
{
    if (!adapter_) return;

    auto modified_ic = ic;
    bool on_ground = modified_ic.altitude_msl_m < terrain_elev_m + 50.0;
    if (on_ground) {
        modified_ic.altitude_msl_m = terrain_elev_m + gear_cg_height_m_;
    }

    // Apply IC (RunIC resets terrain property)
    adapter_->apply_initial_conditions(modified_ic);

    // Set terrain AFTER RunIC so it sticks
    double terrain_ft = terrain_elev_m * 3.28084;
    adapter_->set_property("position/terrain-elevation-asl-ft", terrain_ft);

    RCLCPP_INFO(this->get_logger(),
        "IC applied: terrain=%.1fm, CG=%.1fm, gear_offset=%.2fm, on_ground=%s",
        terrain_elev_m, modified_ic.altitude_msl_m, gear_cg_height_m_,
        on_ground ? "true" : "false");
}
```

- [ ] **Step 6: Remove gear_cg_height_m parameter (no longer from launch file)**

Remove `this->declare_parameter<double>("gear_cg_height_m", 1.8);` and the `get_parameter` call for it. The value now comes from config.yaml.

- [ ] **Step 7: Build**

```bash
colcon build --packages-select flight_model_adapter && source install/setup.bash
```
Expected: builds clean. May need to add yaml-cpp and ament_index_cpp to CMakeLists.txt find_package and link.

- [ ] **Step 8: Commit**

```bash
git add src/core/flight_model_adapter/src/flight_model_adapter_node.cpp src/core/flight_model_adapter/CMakeLists.txt
git commit -m "flight_model_adapter: terrain ready publisher, gear height from config, simplified IC flow"
```

---

### Task 4: cigi_bridge — SOF parsing + HOT rate gating bypass during REPOSITIONING

**Files:**
- Modify: `src/core/cigi_bridge/src/cigi_host_node.cpp`
- Modify: `src/core/cigi_bridge/include/cigi_bridge/cigi_host_node.hpp`

**Context:** The cigi_bridge sends CIGI packets to the IG and receives responses. Read the existing `receive_from_ig()` method and the HOT rate gating logic. The bridge subscribes to `/sim/state` for sim state.

- [ ] **Step 1: Parse SOF IG Status from IG responses**

In the receive handler where SOF packets are currently silently consumed (around line 533), extract the IG Status:
```cpp
// SOF (pkt_id == 0x01) — extract IG Status
case 0x01:
    if (pkt_size >= 8) {
        uint8_t ig_mode = pkt[4] & 0x03;  // bits 0-1 of byte 4
        ig_status_ = ig_mode;  // 0=Standby, 1=Operate, 2=Debug, 3=Offline
    }
    break;
```

Add member: `uint8_t ig_status_ = 0;`

- [ ] **Step 2: Bypass HOT rate gating during REPOSITIONING**

Find the HOT rate gating code. During REPOSITIONING state, always send HOT requests for all gear points regardless of AGL.

In the HOT request logic, add:
```cpp
bool force_hot = (sim_state_ == sim_msgs::msg::SimState::STATE_REPOSITIONING && ig_status_ == 1);
```

Use `force_hot` to bypass the AGL-based rate gating. Only send when IG is Operate (1) — don't probe while terrain is still loading.

- [ ] **Step 3: Build**

```bash
colcon build --packages-select sim_cigi_bridge && source install/setup.bash
```

- [ ] **Step 4: Commit**

```bash
git add src/core/cigi_bridge/src/cigi_host_node.cpp src/core/cigi_bridge/include/cigi_bridge/cigi_host_node.hpp
git commit -m "cigi_bridge: parse SOF IG Status, bypass HOT rate gating during REPOSITIONING"
```

---

### Task 5: X-Plane plugin — scenery load monitoring + SOF IG Status

**Files:**
- Modify: `~/x-plane_plugins/xplanecigi/XPluginMain.cpp`

**Context:** The plugin already handles Entity Control (0x03), HOT requests (0x18), and sends SOF. Read the existing `send_sof()`, `process_packet()`, and `XPluginStart()` functions.

- [ ] **Step 1: Add scenery load dataref + repositioning detection**

In `XPluginStart()`, find the dataref:
```cpp
g_scenery_loading = XPLMFindDataRef("sim/graphics/scenery/async_scenery_load_in_progress");
```

Add globals:
```cpp
static XPLMDataRef g_scenery_loading = nullptr;
static bool g_repositioning = false;
static int g_scenery_stable_frames = 0;
static double g_prev_lat = 0.0;
static double g_prev_lon = 0.0;
```

- [ ] **Step 2: Detect position jump in Entity Control handler**

In the `case 0x03:` handler, after parsing lat/lon, add:
```cpp
// Detect large position change (>0.01 deg ~ 1km)
double dlat = std::abs(lat - g_prev_lat);
double dlon = std::abs(lon - g_prev_lon);
if (dlat > 0.01 || dlon > 0.01) {
    g_repositioning = true;
    g_scenery_stable_frames = 0;
}
g_prev_lat = lat;
g_prev_lon = lon;
```

- [ ] **Step 3: Monitor scenery loading in flight loop**

In the flight loop callback (the function registered with `XPLMRegisterFlightLoopCallback`), add:
```cpp
if (g_repositioning && g_scenery_loading) {
    int loading = XPLMGetDatai(g_scenery_loading);
    if (loading == 0) {
        g_scenery_stable_frames++;
        if (g_scenery_stable_frames >= 3) {
            g_repositioning = false;
        }
    } else {
        g_scenery_stable_frames = 0;
    }
}
```

- [ ] **Step 4: Update SOF with IG Status**

In `send_sof()`, change the IG Mode field:
```cpp
// IG Mode: Operate (2) when ready, Standby (0) when repositioning/loading
buf[4] = g_repositioning ? 0x00 : 0x02;
```

Note: CIGI 3.3 SOF byte 4 bits[1:0] = IG Mode. 0=Reset/Standby, 2=Operate. The cigi_bridge reads this to know when terrain is ready.

- [ ] **Step 5: Add repositioning overlay**

Add a draw callback:
```cpp
static int draw_callback(XPLMDrawingPhase, int, void *)
{
    if (g_repositioning) {
        float color[] = {1.0f, 0.5f, 0.0f};  // amber
        int w, h;
        XPLMGetScreenSize(&w, &h);
        XPLMDrawString(color, w / 2 - 80, h / 2, "REPOSITIONING...",
                       NULL, xplmFont_Proportional);
    }
    return 1;
}
```

Register in `XPluginStart()`:
```cpp
XPLMRegisterDrawCallback(draw_callback, xplm_Phase_Window, 0, NULL);
```

Add include: `#include <XPLMDisplay.h>`

Deregister in `XPluginStop()`:
```cpp
XPLMUnregisterDrawCallback(draw_callback, xplm_Phase_Window, 0, NULL);
```

- [ ] **Step 6: Commit**

```bash
git add ~/x-plane_plugins/xplanecigi/XPluginMain.cpp
git commit -m "xplanecigi: scenery load monitoring, SOF IG Status, repositioning overlay"
```

---

### Task 6: IOS frontend — REPOSITIONING state display

**Files:**
- Modify: `ios/frontend/src/store/useSimStore.js`
- Modify: `ios/frontend/src/components/StatusStrip.jsx`
- Modify: `ios/frontend/src/components/ActionBar.jsx`

- [ ] **Step 1: Update ActionBar to disable buttons during REPOSITIONING**

In `ios/frontend/src/components/ActionBar.jsx`, find the `runDisabled` and `freezeDisabled` logic. Add REPOSITIONING as a disabled state:
```javascript
const freezeDisabled = !wsConnected || simState !== 'RUNNING'
const runDisabled = !wsConnected || simState === 'RUNNING' || simState === 'REPOSITIONING' ||
    (simState !== 'READY' && simState !== 'FROZEN')
```

- [ ] **Step 2: Add REPOSITIONING badge styling**

In `ios/frontend/src/components/StatusStrip.jsx`, find the badge color mapping. Add:
```javascript
REPOSITIONING: { bg: '#7c3aed', text: '#f5f3ff' },  // purple
```

- [ ] **Step 3: System nodes — verify REPOSITIONING is treated as FROZEN**

All system nodes check `sim_state_ == STATE_RUNNING` to decide whether to update. REPOSITIONING (6) is not RUNNING (2), so they'll naturally hold. No code change needed — but verify by reading electrical_node.cpp and gear_node.cpp state checks.

- [ ] **Step 4: Commit**

```bash
git add ios/frontend/src/components/ActionBar.jsx ios/frontend/src/components/StatusStrip.jsx
git commit -m "ios: REPOSITIONING state display — purple badge, buttons disabled"
```

---

### Task 7: Clean up temporary code from this session

**Files:**
- Modify: `src/core/flight_model_adapter/src/flight_model_adapter_node.cpp`
- Modify: `src/aircraft/c172/config/config.yaml`

- [ ] **Step 1: Remove gear_cg_offset_m from config.yaml**

Remove `gear_cg_offset_m: 1.63` from `src/aircraft/c172/config/config.yaml`. The gear height is now computed from `gear_points` z_m values in Task 3.

- [ ] **Step 2: Remove gear_cg_height_m parameter declaration**

Remove `this->declare_parameter<double>("gear_cg_height_m", 1.8);` from flight_model_adapter_node.cpp if it still exists.

- [ ] **Step 3: Build full workspace**

```bash
colcon build --symlink-install
```
Expected: all packages build clean.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "cleanup: remove temporary gear height hacks, use config.yaml gear_points"
```

---

### Task 8: Integration test

- [ ] **Step 1: Start sim without IG**

```bash
./start_sim.sh
```
Wait for "All 9 required nodes alive".

- [ ] **Step 2: Start backend**

```bash
./start_backend.sh
```
Wait 5s for DDS discovery.

- [ ] **Step 3: Test SRTM path**

Open IOS at http://localhost:5173. Navigate to POS tab.
1. Search for an airport (e.g. EBBR)
2. Select a runway
3. Click RWY position icon
4. Click SET POSITION (double-tap confirm)

Expected:
- Status strip shows "REPOSITIONING" briefly (~0.5s for SRTM)
- Then shows "READY"
- Aircraft altitude in status strip matches airport elevation + gear height
- sim_manager log shows "Terrain ready — exiting REPOSITIONING"

- [ ] **Step 4: Test RUN after reposition**

Click RUN. Expected:
- Status strip shows "RUNNING"
- Clock advances
- JSBSim runs (controls.html gauges respond)

- [ ] **Step 5: Test CIGI path (if X-Plane available)**

Start X-Plane with the CIGI plugin. Reposition to a distant airport.

Expected:
- X-Plane shows "REPOSITIONING..." overlay
- IOS shows "REPOSITIONING" for 1-3s (terrain loading)
- Overlay disappears, IOS shows "READY"
- Aircraft is on the ground at correct height

- [ ] **Step 6: Final commit**

```bash
git add DECISIONS.md CLAUDE.md
git commit -m "docs: update DECISIONS.md and CLAUDE.md for repositioning pipeline"
```
