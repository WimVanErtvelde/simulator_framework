# xplanecigi Cloud Rate-Limited Walker — Design

**Status**: approved 2026-04-25, ready for implementation
**Scope**: `x-plane_plugins/xplanecigi/XPluginMain.cpp` only
**Builds on**: blend-at-ownship workaround for KL-1 (DECISIONS.md 2026-04-23 / 2026-04-24)

## Goal

Make weather-patch clouds actually render in flight without spamming X-Plane's `sim/weather/region/*` cloud datarefs every frame. Per Laminar's
[Weather Datarefs in X-Plane 12](https://developer.x-plane.com/article/weather-datarefs-in-x-plane-12/):

> *"Clouds are handled a little differently because there is a delay between the cloud data being determined and the results of the GPU-based cloud simulation being returned. Even with `update_immediately` set to true, clouds will generally take a minute or so to update."*

User confirmed empirically: writing `cloud_coverage_percent` 0.0 → 1.0 in 0.1 increments every 30 s renders smoothly without hitches. Spamming small-delta values every X-Plane frame, by contrast, prevents the renderer from converging.

## Non-goals

- Touching `XPLMSetWeatherAtAirport` / `XPLMSetWeatherAtLocation`. Both ruled out — KL-1 history confirms they don't render at training radii.
- Changing the patch geometry, transition perimeter (stays 10 % of radius), or `cigi_bridge` host emission.
- Changing visibility behaviour. Vis writes stay per-frame, immediate. They already work.
- Adding new config keys. Walker constants are hardcoded for v1.
- Frontend / backend / ROS topic changes.

## Design

### Two write modes for clouds (revision 2026-04-25)

| Condition | Mode | Behaviour |
|-----------|------|-----------|
| `in_overlap` true (any patch with `0 < weight < 1` at ownship — i.e. flying through a transition zone) | **Walk** | Throttle to one step per `CLOUD_WALK_INTERVAL_S`, walking `g_rendered_cloud` toward target by at most `CLOUD_COVERAGE_STEP` / `CLOUD_ALT_STEP_M`. Skip writes entirely if interval hasn't elapsed. |
| Otherwise (no patch in transition at ownship — fully outside, fully inside, or no patches) | **Snap** | Write target cloud values directly to `g_rendered_cloud` and the datarefs. |

This is purely **geometry-based**, not author-based. Author edits land via the snap path because the target shifts but the geometry hasn't. Movement through a patch boundary engages the walker because the target is sweeping continuously and X-Plane's renderer can't track a moving target.

`regen_weather` still fires under the existing `g_regen_mode` rule (pre-flight or freeze). In flight it never fires — the walker handles cloud rendering by writing slowly enough for X-Plane's pipeline to absorb.

Visibility, temp, pressure, rain, wind and runway writes are unchanged — every frame, gated by `update_immediately` per the existing `g_regen_mode` rule.

### Walker mechanics

For each of the 3 cloud slots, per write event:

- **Both invalid** → no-op.
- **Both valid, type unchanged** → step coverage / base_m / top_m toward target by at most `CLOUD_COVERAGE_STEP` / `CLOUD_ALT_STEP_M`.
- **Both valid, type changed** → snap type. (Cumulus ↔ stratus etc. cannot lerp meaningfully; instructor type edits are rare.)
- **Birth** (target valid, rendered invalid): set `rendered.valid = true`, snap type / base / top, set `coverage = 0`, walk up next tick. Avoids the "spawn full coverage in one frame" event that wastes XP's GPU pipeline.
- **Death** (target invalid, rendered valid): walk coverage down. When `coverage ≤ CLOUD_COVERAGE_STEP` after stepping, flip `rendered.valid = false` and snap to zeros.

### State (new)

```cpp
static PendingWeather::CloudLayer g_rendered_cloud[3] = {};
static double g_last_cloud_write_time = -1e9;
```

`g_rendered_cloud` mirrors the value most recently written to X-Plane's `sim/weather/region/cloud_*` datarefs — the walker's source of truth, separate from `pending_wx` (authored global) and `effective` (per-frame blend target).

The earlier `g_cloud_authored` flag was removed (revision 2026-04-25): the simpler geometry-only rule (`in_overlap`) covers all the same cases without needing decoder hooks.

### Constants (new, hardcoded)

```cpp
static constexpr double CLOUD_WALK_INTERVAL_S = 30.0;   // seconds between in-flight cloud writes
static constexpr float  CLOUD_COVERAGE_STEP   = 0.10f;  // max coverage delta per step (5 min for 0→1)
static constexpr float  CLOUD_ALT_STEP_M      = 200.0f; // max base/top delta per step
```

### Per-tick logic in `WeatherFlightLoopCb` (replacing current cloud-write block in Pass 2)

```text
target = effective.cloud[3]   // already computed by blend_weather_at_ownship
in_overlap = any_patch_in_transition_at_ownship()  // hoisted, also used by snapshot

if !in_overlap:
    snap g_rendered_cloud := target
    write all four cloud datarefs from g_rendered_cloud
    g_last_cloud_write_time := now
else:
    if (now - g_last_cloud_write_time) < CLOUD_WALK_INTERVAL_S:
        skip cloud writes this tick
    else:
        walk g_rendered_cloud one step toward target per the per-slot rules above
        write all four cloud datarefs from g_rendered_cloud
        g_last_cloud_write_time := now
```

The existing `regen_weather` firing rule (cloud_changed AND gate_ok AND ≥ 2 s debounce) is unchanged. In flight under default `ground_only`, regen never fires, so cloud rendering relies entirely on X-Plane's own ~1-minute incorporation cadence — which the walker's 30 s/0.1-step regime accommodates.

### Birth / death pseudo-code

```cpp
auto walk_slot = [](PendingWeather::CloudLayer & dst,
                    const PendingWeather::CloudLayer & target) {
    if (!target.valid && !dst.valid) return;

    if (target.valid && !dst.valid) {           // BIRTH
        dst.valid    = true;
        dst.type_xp  = target.type_xp;
        dst.base_m   = target.base_m;
        dst.top_m    = target.top_m;
        dst.coverage = 0.0f;                    // walk up from 0 next tick
        return;
    }

    if (!target.valid && dst.valid) {           // DEATH
        dst.coverage = std::max(0.0f, dst.coverage - CLOUD_COVERAGE_STEP);
        if (dst.coverage <= CLOUD_COVERAGE_STEP * 0.5f) {
            dst = {};                           // {valid=false, all zero}
        }
        return;
    }

    // Both valid — walk
    if (dst.type_xp != target.type_xp) dst.type_xp = target.type_xp;  // snap
    dst.coverage = step_to(dst.coverage, target.coverage, CLOUD_COVERAGE_STEP);
    dst.base_m   = step_to(dst.base_m,   target.base_m,   CLOUD_ALT_STEP_M);
    dst.top_m    = step_to(dst.top_m,    target.top_m,    CLOUD_ALT_STEP_M);
};
```

`step_to(current, target, step) = clamp(current + sign(target - current) * min(|target - current|, step), …)`.

## Files touched

- **`x-plane_plugins/xplanecigi/XPluginMain.cpp`** — only file. Adds two state vars, three constants, the cloud writer, a `wx_log()` timestamped log helper, and a 1Hz weather snapshot block.

No other files (frontend, backend, message defs, cigi_bridge, weather_solver) are touched.

### Debug logging

All log lines from the weather pipeline get an X-Plane-elapsed-time prefix `[%9.3f]` via `wx_log()`. Once per second, regardless of whether the dirty block ran, a snapshot dumps the complete state being asked of X-Plane:

```
[ 12345.678] WX scal vis=800m temp=15.0C pres=1013.2hPa hum=50% rain=10% rwy=0
[ 12345.678] WX cloud[0] V type=2 cov=0.80 base=1500m top=2000m
[ 12345.678] WX cloud[1] -
[ 12345.678] WX cloud[2] -
[ 12345.678] WX wind layers=3 (alt0/dir/spd m/s/turb)
[ 12345.678] WX wind[0] alt=0m dir=270 spd=5.0m/s vert=0.0m/s turb=0.00
[ 12345.678] WX wind[1] alt=1000m dir=280 spd=12.0m/s vert=0.0m/s turb=0.10
[ 12345.678] WX mode overlap=1 apply_now=0 frozen=0 ground=0 walk_age=12.3s patches=2 blend_dirty=0
```

Cloud values come from `g_rendered_cloud` (what was actually written), so the snapshot honestly reflects walker state vs target.

## What stays the same

- `pending_wx` (authored global cache) ↔ `effective` (per-tick blend output) separation.
- Vis Pass 1 — every frame, immediate.
- Pass 2 for non-cloud fields — every frame, gated `update_immediately`.
- `regen_weather` firing rule.
- Blend triggers (`g_blend_dirty`, zone crossings, in-transition).
- Plugin config flags (`plugin_weather_mode = blend_at_ownship` default; `regen_weather = ground_only` default).
- Patch transition perimeter (`10 % of radius` from `cigi_bridge::weather_sync.cpp`).

## What changes observable

- **Patch entry while in flight**: clouds gradually appear over ~5 min as `g_rendered_cloud` walks toward the patch's coverage. No hitch.
- **Patch exit while in flight**: clouds gradually fade over ~5 min.
- **Inside patch (fully, w=1), instructor edits cloud cover via IOS while in flight**: snap path — target lands instantly. X-Plane's own ~1-min GPU pipeline still applies between dataref write and visible pixels, but the value isn't crawling. No hitch from us.

- **In transition zone (0 < w < 1) when an edit lands**: walker continues, target moves to new value, walker walks toward it. Slow but acceptable corner case — instructor isn't usually editing while crossing the boundary at speed.
- **Same edit on ground or under freeze**: instant — `apply_now_ok` is true, snap + regen fires, X-Plane renders within a few seconds.
- **Global cloud edit on ground / freeze**: instant (unchanged from today).
- **Global cloud edit in flight**: snap path — written instantly, X-Plane fades it in over its own ~1 min cadence. The geometry-based rule covers this: `in_overlap` is false because the change is global, not patch-related, so snap.

## Acceptance criteria

1. Plugin builds clean with no new warnings.
2. Existing `cigi_session` tests still pass (15/15) — no test changes expected since this is plugin-only.
3. Manual test: with `plugin_weather_mode=blend_at_ownship` and `regen_weather=ground_only` (defaults):
   - Author a patch with 80 % cumulus, fly into it. Observe clouds gradually appearing over a few minutes. Verify no hitches.
   - Fly out. Observe clouds gradually fading over a few minutes.
   - Pause sim while inside a patch and edit cloud coverage. Verify instant snap + regen hitch (acceptable because paused).
   - Edit GLOBAL clouds while flying. Verify smooth fade-in over ~5 min, no hitch.

## Risks / known limitations

- **5-minute build-up may be slow** for short fly-throughs. **User-confirmed acceptable** because patches write to X-Plane's GLOBAL cloud field anyway (no localised cloud rendering at training radii — KL-1). Use case is "start clean, fly toward cloudy area" where 5 min ramp is realistic and even desirable.
- **Walker is monotonic per step** — if instructor changes target rapidly the walker will lag by up to `CLOUD_WALK_INTERVAL_S × steps_to_converge`. In flight this is by design.
- **Type changes still snap.** XP cloud type transitions (cumulus → stratus) may pop visually. Instructor type edits are rare and usually paired with sim freeze anyway.
- **No new tests added.** Cloud walker behaviour is timing-dependent and difficult to unit-test against X-Plane's renderer; manual smoke is the verification path.

## Rollback

Revert is a single-commit revert. Walker logic is fully self-contained in `WeatherFlightLoopCb`; reverting restores the per-frame cloud writes that exist in the base commit.
