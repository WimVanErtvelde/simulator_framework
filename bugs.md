# Bug Report — Repositioning Pipeline Review

Reviewed: 2026-03-22
Source: Full codebase review of uncommitted changes on `master`

---

## CRITICAL — ACTIVE INVESTIGATION

### 0. Position flicker: JSBSim get_state() returns alternating lat/lon after reposition
**Status: ROOT CAUSE IDENTIFIED, FIX NEEDED**

**Symptom:** After a reposition command, the X-Plane visual flickers between the OLD position
and the NEW position at ~60Hz. The aircraft appears at two locations 30km apart alternating
every frame. X-Plane becomes nearly unresponsive due to constant scenery tile loading.

**Verified facts (from diagnostic logs 2026-03-22):**

1. `apply_initial_conditions` + `RunIC()` DOES move JSBSim to the correct new position:
   ```
   [IC] VERIFY post-apply: JSBSim lat=51.192298° (requested lat=51.192298°) ✓
   ```

2. But Entity Control packets sent to X-Plane ALTERNATE between old and new every frame:
   ```
   ENTITY lat=50.913288  ← OLD (EBBR)
   ENTITY lat=51.192298  ← NEW (Antwerp)
   ENTITY lat=50.913288  ← OLD again
   ENTITY lat=51.192297  ← NEW again
   ```

3. Even before reposition, there's a tiny 0.000002° lat oscillation at the initial position
   (caused by `sim_state_ == 255` → `should_step = true` running JSBSim FDM at startup).

4. The X-Plane plugin's `g_prev_lat` oscillates between both positions, so terrain probes
   alternate between 27.8m (old) and 6.6m (new), never stabilizing.

5. The stale HOT response (27.8m terrain from old position) is accepted for the new position
   because the stale position check was removed.

**Root cause hypothesis:**
JSBSim's `get_state()` (which reads `position/lat-geod-rad`) returns alternating values
frame-to-frame even though nothing explicitly modifies JSBSim's propagation state during
FROZEN/READY (no `step()`, no `RunIC()`). The alternation is between:
- The position after the last `apply_initial_conditions` + `RunIC()` (the new IC position)
- The position from the previous `RunIC()` (the old IC position, or some cached state)

Possible causes to investigate:
- JSBSim FGLocation cache invalidation issue after `SetAltitudeASL` / `SetRadius`
- Double `get_state()` call per timer tick (once in `update_terrain_elevation`, once in main loop)
  with `set_property("position/terrain-elevation-asl-ft")` between them
- `refine_terrain_altitude`'s `SetPropertyValue("position/h-sl-ft")` → `SetAltitudeASL` →
  `SetRadius` subtly corrupting the ECEF direction vector
- JSBSim IC object retaining old state that interferes with propagation reads

**Changes made during investigation (all in uncommitted working tree):**

| File | Change | Purpose |
|---|---|---|
| `JSBSimAdapter.cpp` | Removed `RunIC()` from `refine_terrain_altitude`, replaced with direct property sets | Eliminated geocentric/geodetic roundtrip drift |
| `JSBSimAdapter.cpp` | Added diagnostic `std::cout` after each `RunIC()` in `apply_initial_conditions` | Verify position post-IC (stdout buffered, didn't appear in launch output) |
| `flight_model_adapter_node.cpp` | Added `[IC] VERIFY post-apply` WARN log after `apply_initial_conditions` | Confirmed JSBSim moves to correct position |
| `flight_model_adapter_node.cpp` | Added `[POS]` throttled log in update timer | Used sim-time throttle — didn't fire during FROZEN/READY (clock frozen) |
| `flight_model_adapter_node.cpp` | Removed stale HOT position check (dlat/dlon comparison) | Was rejecting valid HOT because RunIC shifted position. Now accepts stale HOT too. |
| `XPluginMain.cpp` | Added `ENTITY/XPLANE` diagnostic log in Entity Control handler | Revealed the alternating position bug |
| `XPluginMain.cpp` | Wrapped position change detection in `if (!g_repositioning)` | Prevents settle timer reset when position oscillates |
| `XPluginMain.cpp` | Added probe diagnostic logs | Shows probe location and stability |

**Next steps for resolution:**
1. Add WALL-CLOCK throttled diagnostic (`steady_clock`) to FMA update timer to log
   `get_state()` lat/lon EVERY frame and see if JSBSim truly alternates
2. Check JSBSim's `FGLocation::SetRadius()` for cache invalidation
3. Consider: after `apply_initial_conditions`, SAVE the IC position and use it directly
   for Entity Control (bypass get_state() for lat/lon during reposition)
4. Fix stale HOT acceptance: re-add position check but compare against get_state() position
   (not pending_ic_ position), or clear terrain_hot_ after reposition completes
5. Fix startup oscillation: don't run `step()` when `sim_state_ == 255`

---

## CRITICAL — FIXED

### ~~1. Format string crash on every IC receipt~~
**FIXED** — `flight_model_adapter_node.cpp:134-138`

Format string had 5 specifiers but 6 arguments. `%s` consumed `airspeed_ms` (a double) as a char pointer → undefined behavior / segfault on every reposition. Added missing `spd=%.1fm/s` specifier.

### ~~2. Frontend command map sends wrong commands — "Reset Failures" shuts down the sim~~
**FIXED** — `ios_backend_node.py:226-237`

The `_cmd_map` was updated to new ROS2 constants but the keys still represented the frontend's numbering scheme. "Reset Failures" (key 5) mapped to `CMD_SHUTDOWN`, "Shutdown" (key 6) mapped to `CMD_SET_IC`. Restored correct mapping: keys 3/4/5 → `CMD_RESET`, key 6 → `CMD_SHUTDOWN`.

---

## HIGH

### 3. `pending_ic_` never times out — blocks terrain updates, allows late reposition
**`flight_model_adapter_node.cpp:90-107, 331`**

If no CIGI HOT arrives (no IG, IG in Standby, network down), `pending_ic_` is never cleared. Consequences:
- `update_terrain_elevation()` is permanently suppressed (line 331: `if (!pending_ic_)`)
- A late-arriving HOT response could reposition the aircraft mid-flight
- sim_manager has a 15s timeout, but FMA doesn't know about it

### 4. `reposition_active` not forwarded to frontend
**`ios_backend_node.py:301-310`**

The `_on_sim_state` handler builds the WebSocket dict but drops `msg.reposition_active`. The frontend has REPOSITIONING badge code and RUN-button guards, but they check for `simState === 'REPOSITIONING'` — a state that no longer exists. During reposition, UI shows "FROZEN" with all buttons enabled.

### 5. CIGI bridge fires high-rate HOT on ANY freeze, not just reposition
**`cigi_host_node.cpp:452-453`**

```cpp
bool repositioning_hot = (sim_state_ == sim_msgs::msg::SimState::STATE_FROZEN);
```

Every instructor freeze triggers 60 Hz HOT requests for all gear points. The `reposition_active` field in SimState exists specifically to distinguish this, but cigi_bridge ignores it — `state_sub_` only reads `msg->state`, not `msg->reposition_active`.

### 6. `begin_reposition()` doesn't guard RESETTING state
**`sim_manager_node.cpp:480-483`**

Only rejects INIT/SHUTDOWN. If CMD_REPOSITION arrives during RESETTING:
1. Saves `pre_reposition_state_ = RESETTING`
2. `transition_to(FROZEN)` fails (invalid transition)
3. Sets `reposition_pending_ = true` anyway → stuck state

### 7. `finish_reposition()` returns to RUNNING without checking node health
**`sim_manager_node.cpp:516-526`**

If a required node died during the reposition, the sim blindly resumes RUNNING with a dead node.

---

## MEDIUM

### 8. `pending_ic_` not cleared on `on_deactivate()`
**`flight_model_adapter_node.cpp:352-365`**

Stale IC persists across deactivate/activate cycles.

### 9. Hardcoded `11` instead of `SimCommand.CMD_REPOSITION`
**`ios_backend_node.py:1313`**

Fragile magic number, inconsistent with all other command usage.

### 10. `ig_status_pub_` not reset in `on_cleanup()`
**`cigi_host_node.cpp:168-177`**

Breaks the cleanup pattern (other 3 publishers are reset).

### 11. `send_fd_` blocking + missing guard
**`cigi_host_node.cpp:573, 494`**

Send socket not non-blocking; second `sendto()` lacks `send_fd_ >= 0` guard.

### 12. Hardcoded `c172` config path in cigi_bridge
**`cigi_host_node.cpp:191`**

HOT silently stops working for non-C172 aircraft.

### 13. `reload_node()` doesn't check success before chaining
**`sim_manager_node.cpp:860-877`**

All 4 lifecycle transitions fire regardless of failure, publishing misleading states.

### 14. Stale `REPOSITIONING` in backend state_names
**`ios_backend_node.py:303`**

`6: 'REPOSITIONING'` is dead code; state was removed from SimState.msg.

---

## LOW

### 15. CLAUDE.md command constants out of date
Still documents `CMD_RUN=1` (old 1-based numbering).

### 16. `force_publish_state_` / `last_published_state_` are dead code
**`sim_manager_node.cpp:163-164`**

Set but never checked as conditions.

### 17. `hot_send_interval_` member is dead code
**`cigi_host_node.hpp:111`**

Declared but never used (local variable used instead).
