import { create } from 'zustand'
import { useSimStore } from './useSimStore'
import {
  globalDraftToWire,
  activeWeatherToGlobalDraft,
  patchIdentityToWire,
  patchOverridesToWire,
  patchesFromBroadcast,
} from '../components/panels/weatherV2/weatherUnits'

// ── Module-level bookkeeping for patch lifecycle (Slice 5c-refactor-I) ────
// Actions deferred until a reserve_patch broadcast assigns patch_id. Each
// entry: { client_id, message, isRemoval }. Pushed by updatePatchIdentity
// or removePatch when patch_id is still null; flushed by syncFromBroadcast
// when broadcast echoes the reserve with a real patch_id.
const _pendingPatchActions = []

// client_ids the user has locally removed but whose remove_patch WS message
// may not have been processed by the backend yet. Filtered out of
// syncFromBroadcast so a stale broadcast doesn't re-introduce them. Pruned
// when the broadcast stops containing that client_id.
const _suppressedClientIds = new Set()

function _sendWs(message) {
  const { ws, wsConnected } = useSimStore.getState()
  if (!wsConnected || !ws) return false
  ws.send(JSON.stringify(message))
  return true
}

function _flushPendingForClient(client_id, patch_id) {
  const toFire = []
  for (let i = _pendingPatchActions.length - 1; i >= 0; i--) {
    if (_pendingPatchActions[i].client_id === client_id) {
      toFire.unshift(_pendingPatchActions.splice(i, 1)[0])
    }
  }
  for (const action of toFire) {
    const payload = {
      ...action.message,
      data: { ...action.message.data, patch_id },
    }
    _sendWs(payload)
    if (action.isRemoval) _suppressedClientIds.add(client_id)
  }
}

// Draft state for WeatherPanelV2.
//
// Session-volatile: lives only in React memory. serverState mirrors the last
// accepted snapshot (optimistically updated on Accept, then corrected by the
// next /world/weather broadcast via syncFromBroadcast). draft !== serverState
// drives the dirty indicator.
//
// Runway condition is NOT part of V2's draft/serverState — it fires
// immediately via set_runway_condition from RunwayField and reads state
// from useSimStore.activeWeather.runwayConditionIdx, matching the WX
// panel's pattern so both UIs stay in sync.

const initialDraft = {
  global: {
    temperature_c:        15.0,
    pressure_hpa:         1013.25,
    visibility_m:         160000,
    humidity_pct:         50,
    precipitation_rate:   0.0,
    precipitation_type:   0,
    cloud_layers:         [],   // {cloud_type, base_agl_ft, thickness_m, coverage_pct}
    wind_layers:          [],   // Slice 5a-iv
    runway_condition_idx: 0,    // 0-15 category × severity index (0=DRY)
  },
  patches: [],
  // Microbursts live on patches: patch.microburst = null | hazard fields.
  // The standalone tab was retired in the 5c followup cleanup; all
  // microbursts now require a parent patch (source_patch_id > 0).
}

const M_TO_FT_CONST = 3.28084
const FT_TO_M_CONST = 0.3048
const NM_TO_M_CONST = 1852

function draftEquals(a, b) {
  return JSON.stringify(a) === JSON.stringify(b)
}

// Aircraft ground elevation (meters MSL) — used as the AGL reference
// for Global tab cloud layers. Derived from the FDM state's altFtMsl
// minus altFtAgl (what the aircraft reports as "above-ground-level" at
// its current position). Returns 0 if FDM hasn't published yet — safe
// default that degrades to "AGL = MSL" rather than crashing.
function aircraftGroundM() {
  const fdm = useSimStore.getState().fdm ?? {}
  const msl = fdm.altFtMsl ?? 0
  const agl = fdm.altFtAgl ?? 0
  return Math.max(0, (msl - agl) * FT_TO_M_CONST)
}

// ── Patch helpers (Slice 5b-ii) ────────────────────────────────────────────
function roleToLabel(role) {
  return {
    departure:   'DEPARTURE',
    destination: 'DESTINATION',
    custom:      'CUSTOM',
  }[role] || String(role || '').toUpperCase()
}

function roleToDefaultRadiusM(role) {
  // Per DECISIONS.md: dep/dest 10 NM, custom 3 NM
  return role === 'custom' ? 3 * NM_TO_M_CONST : 10 * NM_TO_M_CONST
}

// Identity diff — fields that travel on reserve_patch / update_patch_identity.
// Changes here fire update_patch_identity immediately (not batched to Accept).
function normalizePatchIdentityForDiff(p) {
  return JSON.stringify({
    label: p.label, icao: p.icao,
    lat_deg: p.lat_deg, lon_deg: p.lon_deg,
    ground_elevation_m: p.ground_elevation_m, radius_m: p.radius_m,
    role: p.role, patch_type: p.patch_type,
  })
}

// Override diff — fields that travel on update_patch_overrides.
// Changes here are batched to Accept.
function normalizePatchOverridesForDiff(p) {
  return JSON.stringify({
    override_visibility:    !!p.override_visibility,
    visibility_m:           p.visibility_m,
    override_temperature:   !!p.override_temperature,
    temperature_c:          p.temperature_c,
    override_precipitation: !!p.override_precipitation,
    precipitation_rate:     p.precipitation_rate,
    precipitation_type:     p.precipitation_type,
    override_humidity:      !!p.override_humidity,
    humidity_pct:           p.humidity_pct,
    override_pressure:      !!p.override_pressure,
    pressure_hpa:           p.pressure_hpa,
    override_runway:        !!p.override_runway,
    runway_condition_idx:   p.runway_condition_idx,
    cloud_layers: p.cloud_layers || [],
    wind_layers:  p.wind_layers  || [],
  })
}

function identityDiffers(dp, sp) {
  return normalizePatchIdentityForDiff(dp) !== normalizePatchIdentityForDiff(sp)
}

function overridesDiffer(dp, sp) {
  return normalizePatchOverridesForDiff(dp) !== normalizePatchOverridesForDiff(sp)
}

// Identity fields that are authored in the patch UI without an immediate
// side effect on the backend (unlike icao/lat/lon which trigger SRTM
// resolution). These get batched to Accept like overrides so the user
// sees the Accept button light up and can review before commit.
function deferredIdentityDiff(dp, sp) {
  const diff = {}
  if (dp.radius_m !== sp.radius_m) diff.radius_m = dp.radius_m
  return Object.keys(diff).length > 0 ? diff : null
}

// Microburst diff — authored fields only (hazard_id / activation_time are
// server-assigned and excluded). Note: normalizePatchForDiff intentionally
// does NOT include microburst — microbursts flow over their own wire
// messages and shouldn't force a redundant update_patch.
function normalizeMicroburstForDiff(mb) {
  if (!mb) return 'null'
  return JSON.stringify({
    latitude_deg:     mb.latitude_deg,
    longitude_deg:    mb.longitude_deg,
    core_radius_m:    mb.core_radius_m,
    intensity:        mb.intensity,
    shaft_altitude_m: mb.shaft_altitude_m,
  })
}

function microburstsDiffer(a, b) {
  return normalizeMicroburstForDiff(a) !== normalizeMicroburstForDiff(b)
}

// Stable identity match for broadcast sync. Preserves the draft's tab slot
// and selectedLayer target when broadcast echoes the same logical patch.
// - Primary: client_id (set by reserve_patch since 5c-refactor-I)
// - Fallback A: (role, icao) for airport patches (legacy / scenario-loaded)
// - Fallback B: (role, lat/lon) with 0.0001° tolerance for custom patches
function matchDraftPatch(serverPatch, draftPatches) {
  if (serverPatch.client_id) {
    const byCid = draftPatches.find(d => d.client_id === serverPatch.client_id)
    if (byCid) return byCid
  }
  if (serverPatch.patch_type === 'airport') {
    return draftPatches.find(d =>
      d.role === serverPatch.role && d.icao === serverPatch.icao
    )
  }
  const COORD_TOL_DEG = 0.0001
  return draftPatches.find(d =>
    d.role === serverPatch.role &&
    Math.abs((d.lat_deg ?? 0) - (serverPatch.lat_deg ?? 0)) < COORD_TOL_DEG &&
    Math.abs((d.lon_deg ?? 0) - (serverPatch.lon_deg ?? 0)) < COORD_TOL_DEG
  )
}

// Helpers for cloud/wind actions that target either draft.global or a
// patch indexed by client_id. Shared by addCloud/removeCloud/updateCloud
// and their wind twins — keeps the per-target branching compact.
function readLayers(state, client_id, key) {
  if (client_id == null) return state.draft.global[key] ?? []
  const p = state.draft.patches.find(pp => pp.client_id === client_id)
  return p?.[key] ?? []
}

function applyLayers(state, client_id, key, nextLayers) {
  if (client_id == null) {
    return {
      draft: {
        ...state.draft,
        global: { ...state.draft.global, [key]: nextLayers },
      },
    }
  }
  return {
    draft: {
      ...state.draft,
      patches: state.draft.patches.map(p =>
        p.client_id === client_id ? { ...p, [key]: nextLayers } : p
      ),
    },
  }
}

function tabIdFor(client_id) {
  return client_id == null ? 'global' : client_id
}

export const useWeatherV2Store = create((set, get) => ({
  draft:       structuredClone(initialDraft),
  serverState: structuredClone(initialDraft),

  // Note: no global unit toggle. Aviation convention uses fixed units per
  // field (ft altitudes, kt speeds, °C temperature, hPa pressure). Per-field
  // inline toggles for hPa/inHg, m/SM visibility, etc. land in the slices
  // that introduce those fields.
  activeTab: 'global',

  // Clearing selection on tab change avoids the LayerPropertiesColumn
  // rendering a layer from a different tab (indices don't map across).
  setActiveTab: (tab) => set({ activeTab: tab, selectedLayer: null }),

  // ── Pending tabs (Slice 5c-refactor-I) ─────────────────────────────────
  // Tab state for +DEP / +DEST clicked but not yet committed via airport
  // pick. Pure UI — not part of draft/serverState (no wire representation).
  // When the user picks an airport inside a pending tab, addPatch fires
  // reserve_patch and closePendingTab removes the role from this set.
  pendingTabs: new Set(),

  openPendingTab: (role) => set((state) => {
    const next = new Set(state.pendingTabs)
    next.add(role)
    return { pendingTabs: next, activeTab: role, selectedLayer: null }
  }),

  closePendingTab: (role) => set((state) => {
    const next = new Set(state.pendingTabs)
    next.delete(role)
    return {
      pendingTabs: next,
      activeTab: state.activeTab === role ? 'global' : state.activeTab,
    }
  }),

  // Generic draft mutator. Tabs call with a path into the draft:
  //   updateDraft(['global', 'temperature_c'], 20)
  updateDraft: (path, value) => set((state) => {
    const next = structuredClone(state.draft)
    let cursor = next
    for (let i = 0; i < path.length - 1; i++) cursor = cursor[path[i]]
    cursor[path[path.length - 1]] = value
    return { draft: next }
  }),

  // Called when useSimStore.activeWeather changes (a weather_state or
  // patches broadcast arrived). Under 5c-refactor-I the patch lifecycle
  // commits identity on-change (reserve_patch / update_patch_identity),
  // so the frontend never carries a long-lived patch_id=null state — just
  // a ~10ms window between reserve_patch fire and broadcast echo. The
  // reconciliation here:
  //  - filters broadcast by _suppressedClientIds (locally-removed patches)
  //  - matches broadcast patches to draft by client_id (primary) with
  //    icao/coord fallback for legacy/scenario-loaded patches
  //  - backfills patch_id + ground_elevation_m onto matched draft patches
  //  - flushes _pendingPatchActions for any just-backfilled client_ids
  //  - for CLEAN drafts, replaces draft.patches wholesale from broadcast
  //  - for DIRTY drafts (override authoring in flight), preserves draft
  //    overrides but still accepts identity+patch_id updates from server
  syncFromBroadcast: (activeWeather) => {
    const state = get()
    const groundM      = aircraftGroundM()
    const freshGlobal  = activeWeatherToGlobalDraft(activeWeather, groundM)
    const rawPatches   = patchesFromBroadcast(activeWeather?.patches)

    // Suppress locally-removed patches. Prune entries whose client_ids the
    // broadcast no longer contains (backend has caught up with the delete).
    const broadcastCids = new Set(rawPatches.map(p => p.client_id))
    for (const cid of [..._suppressedClientIds]) {
      if (!broadcastCids.has(cid)) _suppressedClientIds.delete(cid)
    }
    const visiblePatches = rawPatches.filter(sp => !_suppressedClientIds.has(sp.client_id))

    // Microburst reconciliation: index broadcast hazards by source_patch_id
    // so they can be attached to their parent patch. source_patch_id=0
    // entries (from legacy scenarios pre-standalone-removal) are ignored.
    const mbList       = activeWeather?.microbursts ?? []
    const mbByPatchId  = new Map()
    for (const raw of mbList) {
      const pid = raw.source_patch_id ?? 0
      if (pid === 0) continue
      mbByPatchId.set(pid, {
        latitude_deg:     raw.latitude_deg,
        longitude_deg:    raw.longitude_deg,
        core_radius_m:    raw.core_radius_m,
        intensity:        raw.intensity,
        shaft_altitude_m: raw.shaft_altitude_m,
      })
    }

    // Flush queued actions for any reserve that just assigned patch_id.
    // Detect by matching each broadcast patch against a draft patch whose
    // current patch_id is null.
    for (const sp of visiblePatches) {
      if (sp.patch_id == null) continue
      const matched = matchDraftPatch(sp, state.draft.patches)
      if (matched && matched.patch_id == null) {
        _flushPendingForClient(matched.client_id, sp.patch_id)
      }
    }

    const freshPatches = visiblePatches.map(sp => {
      const matched = matchDraftPatch(sp, state.draft.patches)
      const mbFromBroadcast = (sp.patch_id != null)
        ? (mbByPatchId.get(sp.patch_id) ?? null)
        : null
      return matched
        ? { ...sp, client_id: matched.client_id, microburst: mbFromBroadcast }
        : { ...sp, microburst: mbFromBroadcast }
    })

    const wasClean = draftEquals(state.draft, state.serverState)
    const nextServer = {
      ...state.serverState,
      global:  { ...state.serverState.global, ...freshGlobal },
      patches: freshPatches,
    }
    const nextDraft = wasClean
      ? {
          ...state.draft,
          global:  { ...state.draft.global, ...freshGlobal },
          patches: freshPatches,
        }
      : {
          ...state.draft,
          // Dirty draft: preserve user's override authoring per-patch but
          // still accept server-authoritative fields (patch_id + ground
          // elevation). Identity fields are committed on-change under the
          // refactor, so draft identity is already in sync — the backfill
          // path is only needed for the patch_id-null window.
          patches: state.draft.patches.map(dp => {
            const match = freshPatches.find(fp => fp.client_id === dp.client_id)
            if (!match) return dp
            return {
              ...dp,
              patch_id:           match.patch_id,
              ground_elevation_m: match.ground_elevation_m,
            }
          }),
        }
    set({ serverState: nextServer, draft: nextDraft })
  },

  // Commits the draft to the backend. Under 5c-refactor-I, patch identity
  // and removals commit on their respective actions (addPatch fires
  // reserve_patch; updatePatchIdentity fires update_patch_identity;
  // removePatch fires remove_patch). Accept only fires:
  //  - set_weather (global scalars, always)
  //  - update_patch_overrides per patch with override-side diff
  //  - set/clear_patch_microburst per patch with microburst diff
  accept: () => {
    const state = get()
    if (draftEquals(state.draft, state.serverState)) return

    const { ws, wsConnected } = useSimStore.getState()
    if (!wsConnected || !ws) {
      console.warn('[WeatherV2] accept(): WS not connected — commit dropped')
      return
    }

    // 1. Global scalars.
    ws.send(JSON.stringify({
      type: 'set_weather',
      data: globalDraftToWire(state.draft.global, aircraftGroundM()),
    }))

    // 2. Patches — override diffs only. Patches with patch_id==null are
    //    still mid-reserve; their overrides (all disabled by default at
    //    reserve time) will match serverState and not trigger a diff.
    const draftPatches  = state.draft.patches  ?? []
    const serverById    = new Map(
      (state.serverState.patches ?? [])
        .filter(p => p.patch_id != null)
        .map(p => [p.patch_id, p])
    )
    for (const dp of draftPatches) {
      if (dp.patch_id == null) continue
      const sp = serverById.get(dp.patch_id)
      if (!sp) continue

      // Deferred identity fields (radius_m) — Accept is the commit point,
      // same as overrides. icao/lat/lon commit immediately via
      // updatePatchIdentity so they don't reach here.
      const idDiff = deferredIdentityDiff(dp, sp)
      if (idDiff) {
        ws.send(JSON.stringify({
          type: 'update_patch_identity',
          data: { patch_id: dp.patch_id, ...idDiff },
        }))
      }

      if (overridesDiffer(dp, sp)) {
        ws.send(JSON.stringify({
          type: 'update_patch_overrides',
          data: patchOverridesToWire(dp),
        }))
      }
    }

    // 3. Per-patch microbursts. Skip patches with patch_id==null; once the
    //    broadcast assigns the id, the next Accept picks them up.
    for (const dp of draftPatches) {
      if (dp.patch_id == null) continue
      const sp = serverById.get(dp.patch_id)
      if (!sp) continue
      if (!microburstsDiffer(dp.microburst, sp.microburst)) continue
      if (dp.microburst) {
        ws.send(JSON.stringify({
          type: 'set_patch_microburst',
          data: { patch_id: dp.patch_id, ...dp.microburst },
        }))
      } else {
        ws.send(JSON.stringify({
          type: 'clear_patch_microburst',
          data: { patch_id: dp.patch_id },
        }))
      }
    }

    // Optimistic commit. Next broadcast reconciles authoritative fields.
    set((s) => ({ serverState: structuredClone(s.draft) }))
  },

  // Under 5c-refactor-I, identity changes commit on-change (fire WS + mirror
  // to serverState), so discard has nothing at the wire level to revert —
  // only local override authoring diffs between draft and serverState.
  // draft := serverState is sufficient.
  discard: () => set((state) => ({ draft: structuredClone(state.serverState) })),

  // ── Layer selection (drives LayerPropertiesColumn content) ─────────────
  // null when nothing selected; empty-state shown. tabId is captured at
  // selection time so switching tabs can scope index math correctly
  // (a cloud[2] in DEP and cloud[2] in GLOBAL index into different lists).
  selectedLayer: null,   // { tabId, kind, index } | null

  selectLayer: (kind, index) => set(state => ({
    selectedLayer: { tabId: state.activeTab, kind, index },
  })),
  clearSelection: () => set({ selectedLayer: null }),

  // ── Cloud layer actions (draft only — committed on Accept) ─────────────
  // All take an optional client_id — null/undefined targets draft.global,
  // a patch's client_id targets draft.patches[i].cloud_layers. Cap at 3
  // per target (backend enforces same limit).
  addCloud: (client_id = null) => set((state) => {
    const cur = readLayers(state, client_id, 'cloud_layers')
    if (cur.length >= 3) return state
    // Stack upward: new base = max existing top + 2000 ft, else 3000 AGL.
    let new_base_agl_ft = 3000
    if (cur.length > 0) {
      const maxTop = Math.max(
        ...cur.map(cl => (cl.base_agl_ft ?? 0) + (cl.thickness_m ?? 0) * M_TO_FT_CONST)
      )
      new_base_agl_ft = Math.min(45000, maxTop + 2000)
    }
    const newLayer = {
      cloud_type:   7,                          // Cumulus
      base_agl_ft:  new_base_agl_ft,
      thickness_m:  2000 * FT_TO_M_CONST,
      coverage_pct: 25,
    }
    return applyLayers(state, client_id, 'cloud_layers', [...cur, newLayer])
  }),

  removeCloud: (index, client_id = null) => set((state) => {
    const cur = readLayers(state, client_id, 'cloud_layers')
    const next = cur.filter((_, i) => i !== index)
    // Clear selection if the removed layer was selected in THIS tab; shift
    // index down if a higher-index layer in THIS tab was selected. Layers
    // in other tabs are untouched — their indices are independent.
    const targetTab = tabIdFor(client_id)
    let nextSel = state.selectedLayer
    if (nextSel?.tabId === targetTab && nextSel?.kind === 'cloud') {
      if (nextSel.index === index)      nextSel = null
      else if (nextSel.index > index)   nextSel = { ...nextSel, index: nextSel.index - 1 }
    }
    return { ...applyLayers(state, client_id, 'cloud_layers', next), selectedLayer: nextSel }
  }),

  updateCloud: (index, patch, client_id = null) => set((state) => {
    const cur = readLayers(state, client_id, 'cloud_layers')
    if (index < 0 || index >= cur.length) return state
    const next = cur.map((cl, i) => (i === index ? { ...cl, ...patch } : cl))
    return applyLayers(state, client_id, 'cloud_layers', next)
  }),

  setRunwayConditionIdx: (idx) => set((state) => ({
    draft: {
      ...state.draft,
      global: {
        ...state.draft.global,
        runway_condition_idx: Math.max(0, Math.min(15, Number(idx) || 0)),
      },
    },
  })),

  // ── Wind layer actions (draft only — committed on Accept) ──────────────
  // All take an optional client_id — null/undefined targets draft.global,
  // a patch's client_id targets draft.patches[i].wind_layers. Cap at 13
  // per target (X-Plane WeatherState max).
  addWind: (client_id = null) => set((state) => {
    const cur = readLayers(state, client_id, 'wind_layers')
    if (cur.length >= 13) return state
    // Stack upward: new altitude = max existing + 2000 ft, else 5000 ft MSL.
    let new_alt_m = 5000 * FT_TO_M_CONST
    if (cur.length > 0) {
      const maxAlt = Math.max(...cur.map(wl => wl.altitude_msl_m ?? 0))
      new_alt_m = Math.min(45000 * FT_TO_M_CONST, maxAlt + 2000 * FT_TO_M_CONST)
    }
    const newLayer = {
      altitude_msl_m:       new_alt_m,
      wind_direction_deg:   240,              // prevailing westerly default
      wind_speed_ms:        10 * 0.514444,    // ~10 kt
      vertical_wind_ms:     0.0,
      gust_speed_ms:        0.0,
      shear_direction_deg:  0.0,
      shear_speed_ms:       0.0,
      turbulence_severity:  0.0,
    }
    return applyLayers(state, client_id, 'wind_layers', [...cur, newLayer])
  }),

  removeWind: (index, client_id = null) => set((state) => {
    const cur = readLayers(state, client_id, 'wind_layers')
    const next = cur.filter((_, i) => i !== index)
    const targetTab = tabIdFor(client_id)
    let nextSel = state.selectedLayer
    if (nextSel?.tabId === targetTab && nextSel?.kind === 'wind') {
      if (nextSel.index === index)    nextSel = null
      else if (nextSel.index > index) nextSel = { ...nextSel, index: nextSel.index - 1 }
    }
    return { ...applyLayers(state, client_id, 'wind_layers', next), selectedLayer: nextSel }
  }),

  updateWind: (index, patch, client_id = null) => set((state) => {
    const cur = readLayers(state, client_id, 'wind_layers')
    if (index < 0 || index >= cur.length) return state
    const next = cur.map((wl, i) => (i === index ? { ...wl, ...patch } : wl))
    return applyLayers(state, client_id, 'wind_layers', next)
  }),

  // ── Patch actions (Slice 5c-refactor-I — identity commits on change) ───
  // addPatch fires reserve_patch and mirrors to BOTH draft and serverState
  // so creation doesn't register as dirty (only subsequent override
  // authoring makes the Accept button light up). All overrides start
  // disabled — a reserved-but-unauthored patch is FDM-invisible (weather_
  // solver falls through to global inside the radius).
  //
  // `initial` may include: icao, lat_deg, lon_deg, ground_elevation_m,
  // radius_m, label. For airport patches, caller should pass the
  // AirportSearch hit's {icao, arp_lat_deg, arp_lon_deg, elevation_m} to
  // avoid a backend re-resolve. For custom patches, passing no
  // ground_elevation_m triggers backend SRTM probe + follow-up
  // update_patch_identity.
  addPatch: (role, initial = {}) => {
    const state = get()
    const client_id  = `${role}-${Date.now()}`
    const patch_type = role === 'custom' ? 'custom' : 'airport'
    const globalG = state.draft.global

    const newPatch = {
      client_id,
      patch_id:           null,   // backfilled from broadcast (~10-20ms)
      role,
      patch_type,
      label:              initial.label ?? roleToLabel(role),
      icao:               initial.icao  ?? '',
      lat_deg:            initial.lat_deg ?? 0,
      lon_deg:            initial.lon_deg ?? 0,
      ground_elevation_m: initial.ground_elevation_m ?? 0,
      radius_m:           initial.radius_m ?? roleToDefaultRadiusM(role),

      // All overrides disabled. Value fields seeded from current Global so
      // the UI shows sensible starting numbers when the user flips a toggle.
      override_visibility:    false, visibility_m:       globalG.visibility_m,
      override_temperature:   false, temperature_c:      globalG.temperature_c,
      override_precipitation: false, precipitation_rate: globalG.precipitation_rate,
                                     precipitation_type: globalG.precipitation_type,
      override_humidity:      false, humidity_pct:       globalG.humidity_pct,
      override_pressure:      false, pressure_hpa:       globalG.pressure_hpa,
      override_runway:        false, runway_condition_idx: globalG.runway_condition_idx ?? 0,

      cloud_layers: [],
      wind_layers:  [],
      microburst:   null,
    }

    // Mirror to both draft and serverState — creation is a commit, not
    // a dirty edit. Active tab pivots to the new patch.
    set(s => ({
      draft:       { ...s.draft,       patches: [...s.draft.patches,       newPatch] },
      serverState: { ...s.serverState, patches: [...s.serverState.patches, newPatch] },
      activeTab:   client_id,
    }))

    // Fire reserve_patch. Omit ground_elevation_m if caller didn't pass
    // one (signals backend to SRTM/SearchAirports-resolve + follow up).
    const reserveData = {
      client_id,
      patch_type,
      role,
      label:    newPatch.label,
      icao:     newPatch.icao,
      lat_deg:  newPatch.lat_deg,
      lon_deg:  newPatch.lon_deg,
      radius_m: newPatch.radius_m,
    }
    if (initial.ground_elevation_m != null) {
      reserveData.ground_elevation_m = initial.ground_elevation_m
    }
    _sendWs({ type: 'reserve_patch', data: reserveData })
  },

  // Identity commit-on-change. Mirrors to both draft and serverState so
  // the commit doesn't register as dirty. If patch_id hasn't been backfilled
  // yet (reserve still in flight, very short window), queue the WS message
  // for flush by syncFromBroadcast once patch_id arrives.
  //
  // Pass only the identity fields that changed. Other fields remain intact.
  // When changing icao/lat/lon, omit ground_elevation_m to trigger backend
  // re-resolve (SearchAirports for airport, SRTM for custom).
  updatePatchIdentity: (client_id, identity) => {
    const state = get()
    const patch = state.draft.patches.find(p => p.client_id === client_id)
    if (!patch) return

    set(s => ({
      draft: {
        ...s.draft,
        patches: s.draft.patches.map(p =>
          p.client_id === client_id ? { ...p, ...identity } : p
        ),
      },
      serverState: {
        ...s.serverState,
        patches: s.serverState.patches.map(p =>
          p.client_id === client_id ? { ...p, ...identity } : p
        ),
      },
    }))

    const message = {
      type: 'update_patch_identity',
      data: { ...identity },  // patch_id injected on send
    }
    if (patch.patch_id != null) {
      _sendWs({ ...message, data: { patch_id: patch.patch_id, ...identity } })
    } else {
      _pendingPatchActions.push({ client_id, message })
    }
  },

  // Local-only patch mutator. Used by UI for in-progress authoring (radius
  // slider drag, label typing, override value editing). Does NOT fire WS.
  // For identity fields, callers commit on release/blur via updatePatchIdentity.
  // For override fields, changes are batched to Accept (update_patch_overrides).
  updatePatch: (client_id, patch) => set((state) => ({
    draft: {
      ...state.draft,
      patches: state.draft.patches.map(p =>
        p.client_id === client_id ? { ...p, ...patch } : p
      ),
    },
  })),

  // Fires remove_patch immediately if patch_id is known; otherwise queues
  // until syncFromBroadcast backfills patch_id. Hides the patch from both
  // draft and serverState optimistically, and suppresses it from future
  // broadcast reconciliation until the backend drops it.
  removePatch: (client_id) => {
    const state = get()
    const patch = state.draft.patches.find(p => p.client_id === client_id)
    if (!patch) return

    set(s => ({
      draft: {
        ...s.draft,
        patches: s.draft.patches.filter(p => p.client_id !== client_id),
      },
      serverState: {
        ...s.serverState,
        patches: s.serverState.patches.filter(p => p.client_id !== client_id),
      },
      activeTab: s.activeTab === client_id ? 'global' : s.activeTab,
    }))

    _suppressedClientIds.add(client_id)

    if (patch.patch_id != null) {
      _sendWs({ type: 'remove_patch', data: { patch_id: patch.patch_id } })
    } else {
      _pendingPatchActions.push({
        client_id,
        message: { type: 'remove_patch', data: {} },
        isRemoval: true,
      })
    }
  },

  toggleOverride: (client_id, field, enabled) => set((state) => ({
    draft: {
      ...state.draft,
      patches: state.draft.patches.map(p =>
        p.client_id === client_id
          ? { ...p, [`override_${field}`]: !!enabled }
          : p
      ),
    },
  })),

  // ── Microburst actions (Slice 5c — draft only; Accept emits WS messages) ─
  // Per-patch: lives on patch.microburst. `mb` shape: { latitude_deg,
  // longitude_deg, core_radius_m, intensity, shaft_altitude_m }. Lat/lon
  // resolved client-side from runway threshold + heading + distance
  // (airport patch) or patch center + bearing + distance (custom patch).
  setPatchMicroburst: (client_id, mb) => set((state) => ({
    draft: {
      ...state.draft,
      patches: state.draft.patches.map(p =>
        p.client_id === client_id ? { ...p, microburst: mb } : p
      ),
    },
  })),

  clearPatchMicroburst: (client_id) => set((state) => ({
    draft: {
      ...state.draft,
      patches: state.draft.patches.map(p =>
        p.client_id === client_id ? { ...p, microburst: null } : p
      ),
    },
  })),
}))
