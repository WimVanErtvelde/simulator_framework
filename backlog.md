# Backlog

Non-urgent improvements noted during development. Not yet scheduled.

## Weather

### Smooth-transition patch boundaries
**Status:** Open
**Added:** 2026-04-21 (Slice 5b-iv)

`WeatherPatch.msg` already carries a `transition_perimeter_m` field but
current behavior is a hard step at the radius boundary (inside → 100%
patch, outside → 0% global). Implement a linear blend:

- `dist < radius - transition_perimeter_m` → 100% patch
- `dist >= radius` → 100% global
- In the ramp zone → linear interpolation between patch and global values

Applies to: temperature, pressure, wind interpolation, runway friction,
humidity. `effective_runway_friction` in particular will need a tie-break
rule since it's an integer index (round? bias toward worse condition for
safety training?).

Current step behavior works for training — smoother is more realistic
for XP-style rendering and avoids altimeter jumps when crossing the
radius during an approach.

**Reference:** see `weather_solver.cpp::find_active_patch` and the
per-field override blocks in `compute()`.

### Stable frontend identity for patches (UUID over coord-match)
**Status:** Open
**Added:** 2026-04-21 (Slice 5b-iv custom-patch audit)

Custom patches match draft↔server by `(role, lat_deg, lon_deg)` with a
0.0001° tolerance. This breaks in two scenarios:

- **Move-before-broadcast** (audit finding C2): user creates a custom
  patch, clicks Accept, then drags/re-picks coords BEFORE the broadcast
  ack arrives. Broadcast echoes OLD coords with new patch_id; draft is
  already at NEW coords → `matchDraftPatch` finds no match → broadcast
  patch inserts with `srv-N` client_id while the original draft keeps
  `patch_id: null` → next Accept re-emits `add_patch`, creating a
  duplicate on the backend.
- **Co-located duplicates** (audit finding M1): two custom patches
  within 0.0001° (~11m) collide in `.find()` — the second draft
  inherits the first's identity.

Airport patches don't hit this because `matchDraftPatch` matches by
`(role, icao)` — stable across moves.

**Fix design:** frontend generates a UUID at `addPatch` time, sends it
with `add_patch`. Backend echoes it back in the broadcast (add
`client_uuid: string` to `WeatherPatch.msg` — optional string, empty
when not authored by IOS). `matchDraftPatch` matches by UUID first,
falling back to coords only for legacy patches.

**Blast radius:** msg addition, ios_backend add_patch/update_patch/
_broadcast_patches, frontend `addPatch` + `matchDraftPatch` +
`patchesFromBroadcast`. Medium slice.

### Custom patch coordinate picker (no airport)
**Status:** Open
**Added:** 2026-04-21 (Slice 5b-iv custom-patch audit)

Custom patches currently require going through `AirportSearch` at
creation (`EmptyPatchState.jsx` renders for any patch without an
icao, and only shows an airport picker — no map click / lat-lon entry).
Once initialized, the patch inherits the chosen airport's coords and
the user can rename the label, but there's no direct path to create a
custom patch at arbitrary coordinates.

Options:
1. Map-click placement in the map view (drag to position on the IOS
   map, then commit as a patch). Requires wiring map click → store.
2. Lat/lon entry fields on the PatchHeader for custom patches — simpler
   but less ergonomic.
3. Both.

Medium UX slice. Not blocking — the current path is a visible gap but
works as documented.
