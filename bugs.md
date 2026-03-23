# Bug Report — Repositioning Pipeline Review

Reviewed: 2026-03-22
Updated: 2026-03-23
Source: Full codebase review + debugging session on `master`

---

## CRITICAL — FIXED

### ~~0. Position flicker: Entity Control alternates between old and new position~~
**FIXED** — Root cause: stale `cigi_bridge` zombie process from previous session.

Two cigi_bridge processes sent Entity Control at 60 Hz to X-Plane — one with old EBBR position,
one with the correct new position. X-Plane alternated between them frame-by-frame.

**Root cause was NOT JSBSim** — diagnostic logging proved FMA published stable, correct lat/lon
on every frame. The original hypothesis (FGLocation cache corruption) was wrong.

**Fix:** Kill stale processes before starting (`fuser -k 8001/udp 8002/udp`).
Additional defensive improvements committed (pending_ic_ stepping gate, refine_terrain_altitude
using RunIC, sim_state==255 fix) but were not the actual fix.

### ~~1. Format string crash on every IC receipt~~
**FIXED** — `flight_model_adapter_node.cpp`

### ~~2. Frontend command map sends wrong commands~~
**FIXED** — `ios_backend_node.py`

---

## HIGH — FIXED

### ~~3. `pending_ic_` never times out~~
**FIXED** — 30s wall-clock timeout in FMA update timer. If no CIGI HOT arrives, clears
`pending_ic_` and publishes `terrain_ready(true)`. Covers no-CIGI development and IG failure.
30s chosen because X-Plane tile loading takes 10-20s for cross-continent repositions.

### ~~4. `reposition_active` not forwarded to frontend~~
**FIXED** — ios_backend forwards `bool(msg.reposition_active)` in sim_state WS message.
Frontend store maps to `simState='REPOSITIONING'` which shows purple badge and locks buttons.

### ~~5. CIGI bridge fires high-rate HOT on ANY freeze~~
**FIXED** — cigi_bridge now reads `msg->reposition_active` from SimState. HOT rate:
10 Hz during reposition, AGL-based otherwise. No longer fires 60 Hz on instructor freeze.

### ~~6. `begin_reposition()` doesn't guard RESETTING state~~
**FIXED** — Added `STATE_RESETTING` to the rejection guard in `begin_reposition()`.

### 7. `finish_reposition()` returns to RUNNING without checking node health
**OPEN** — `sim_manager_node.cpp`

If a required node died during the reposition, the sim blindly resumes RUNNING with a dead node.

---

## MEDIUM — FIXED

### ~~8. `pending_ic_` not cleared on `on_deactivate()`~~
**FIXED** — `pending_ic_.reset()` added in `on_deactivate()`.

### ~~9. Hardcoded `11` instead of `SimCommand.CMD_REPOSITION`~~
**FIXED** — Changed to `SimCommand.CMD_REPOSITION` constant.

### ~~10. `ig_status_pub_` not reset in `on_cleanup()`~~
**FIXED** — Added `ig_status_pub_.reset()` in `on_cleanup()`.

### ~~11. `send_fd_` missing guard on second `sendto()`~~
**FIXED** — Added `if (send_fd_ >= 0)` guard on HOT sendto.

### 12. Hardcoded `c172` config path in cigi_bridge
**OPEN** — `cigi_host_node.cpp:191`

HOT silently stops working for non-C172 aircraft. Should use `aircraft_id` parameter.

### 13. `reload_node()` doesn't check success before chaining
**OPEN** — `sim_manager_node.cpp`

All 4 lifecycle transitions fire regardless of failure.

### ~~14. Stale `REPOSITIONING` in backend state_names~~
**FIXED** — Removed dead `6: 'REPOSITIONING'` entry.

---

## LOW — FIXED

### ~~15. CLAUDE.md command constants out of date~~
**FIXED** — Updated to 0-based numbering with CMD_REPOSITION=11.

### ~~16. `force_publish_state_` / `last_published_state_` are dead code~~
**FIXED** — Removed both members and all assignment sites.

### ~~17. `hot_send_interval_` member is dead code~~
**FIXED** — Removed unused member from header.

---

## Additional fixes (found during 2026-03-23 session)

### Stale HOT on reposition
**FIXED** — `hat_tracker_.clear()` on `reposition_active` rising edge. Old in-flight HOT
responses from the previous position can no longer resolve (request IDs invalidated).
Without this fix, stale HOT (e.g. 27.8m from EBBR) was accepted at the new position.

### apt.dat displaced threshold unit bug
**FIXED** — `AirportDatabase.cpp` was multiplying apt.dat displaced threshold (already in metres)
by FT2M (0.3048), shrinking the value to 30%. Removed the conversion.

### Runway placement past piano bars
**FIXED** — Ground positions (RWY icon) offset by `displaced_threshold + 30m` along runway
heading. The 30m clears the piano bar markings.

---

## Summary

| Status | Count |
|--------|-------|
| Fixed  | 17    |
| Open   | 3 (#7, #12, #13) |
