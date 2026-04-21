import { create } from 'zustand'
import { useSimStore } from './useSimStore'
import {
  globalDraftToWire,
  activeWeatherToGlobalDraft,
  patchDraftToAddWire,
  patchDraftToUpdateWire,
  patchesFromBroadcast,
} from '../components/panels/weatherV2/weatherUnits'

// Draft state for WeatherPanelV2.
//
// Session-volatile: lives only in React memory. serverState mirrors the last
// accepted snapshot (optimistically updated on Accept, then corrected by the
// next /world/weather broadcast via syncFromBroadcast). draft !== serverState
// drives the dirty indicator.
//
// Runway condition is NOT part of V2's draft/serverState — it fires
// immediately via set_runway_condition from RunwayField and reads state
// from useSimStore.activeWeather.runwayFriction, matching the WX panel's
// pattern so both UIs stay in sync.

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
    runway_friction:      0,    // 0-15 EURAMEC index (0=DRY)
  },
  patches: [],
  microbursts: [],
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

// Normalize a patch to the subset of fields that travel over the wire.
// Used to decide whether to emit update_patch. Draft-only deferred fields
// (humidity/pressure/runway overrides) are intentionally excluded — the
// msg doesn't carry them, so changes don't warrant an update round-trip.
function normalizePatchForDiff(p) {
  return JSON.stringify({
    label: p.label, icao: p.icao,
    lat_deg: p.lat_deg, lon_deg: p.lon_deg,
    ground_elevation_m: p.ground_elevation_m, radius_m: p.radius_m,
    role: p.role, patch_type: p.patch_type,
    override_visibility:    !!p.override_visibility,
    visibility_m:           p.visibility_m,
    override_temperature:   !!p.override_temperature,
    temperature_c:          p.temperature_c,
    override_precipitation: !!p.override_precipitation,
    precipitation_rate:     p.precipitation_rate,
    precipitation_type:     p.precipitation_type,
    cloud_layers: p.cloud_layers || [],
    wind_layers:  p.wind_layers  || [],
  })
}

function patchesDiffer(dp, sp) {
  return normalizePatchForDiff(dp) !== normalizePatchForDiff(sp)
}

// Stable identity match for broadcast sync: preserve draft's client_id
// when the same logical patch re-appears from the server. Prevents the
// activeTab/selectedLayer from losing its target after add-accept-
// broadcast roundtrip (where the draft patch's patch_id goes null → N).
// - airport patches: match by (role, icao)
// - custom patches:  match by (role, lat, lon) with 0.0001° tolerance
//   (~10 m at equator — tight enough that two distinct custom patches
//   won't collide, loose enough to tolerate float roundtrip through JSON
//   serialization and ROS2 float32 quantization).
function matchDraftPatch(serverPatch, draftPatches) {
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
  // patches broadcast arrived). Merges scalars into serverState.global
  // and patches into serverState.patches. Draft receives the same merge
  // only if it was clean at sync time — never clobbers in-flight edits.
  //
  // Patch client_id preservation: when a broadcast patch matches a draft
  // patch by (role, icao) for airports or (role, lat/lon) for custom, the
  // draft's client_id is carried into the merged patch. This keeps the
  // activeTab / selectedLayer.tabId reference valid across the add →
  // broadcast roundtrip where the server assigns a real patch_id.
  syncFromBroadcast: (activeWeather) => set((state) => {
    const groundM      = aircraftGroundM()
    const freshGlobal  = activeWeatherToGlobalDraft(activeWeather, groundM)
    const rawPatches   = patchesFromBroadcast(activeWeather?.patches)
    const freshPatches = rawPatches.map(sp => {
      const matched = matchDraftPatch(sp, state.draft.patches)
      return matched ? { ...sp, client_id: matched.client_id } : sp
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
      : state.draft
    return { serverState: nextServer, draft: nextDraft }
  }),

  // Commits the draft to the backend. Fires set_weather for global scalars
  // and one add_patch / update_patch / remove_patch per patch-diff entry.
  // Patch diff is computed against serverState.patches (the last-known
  // backend state). Optimistic commit of serverState=draft at the end; the
  // next broadcast will reconcile any backend clamp / new patch_id.
  accept: () => {
    const state = get()
    if (draftEquals(state.draft, state.serverState)) return

    const { ws, wsConnected } = useSimStore.getState()
    if (!wsConnected || !ws) {
      console.warn('[WeatherV2] accept(): WS not connected — commit dropped')
      return
    }

    // 1. Global — fire every Accept for safety; publish_weather merges.
    ws.send(JSON.stringify({
      type: 'set_weather',
      data: globalDraftToWire(state.draft.global, aircraftGroundM()),
    }))

    // 2. Patches — diff draft vs serverState and emit per-delta messages.
    const draftPatches  = state.draft.patches  ?? []
    const serverPatches = state.serverState.patches ?? []
    const serverById    = new Map(serverPatches.map(p => [p.patch_id, p]))
    const draftIds      = new Set(
      draftPatches.filter(p => p.patch_id != null).map(p => p.patch_id)
    )

    // New (patch_id == null) → add_patch
    for (const dp of draftPatches) {
      if (dp.patch_id == null) {
        ws.send(JSON.stringify({
          type: 'add_patch',
          data: patchDraftToAddWire(dp),
        }))
      }
    }

    // Existing — send update_patch only if something changed
    for (const dp of draftPatches) {
      if (dp.patch_id == null) continue
      const sp = serverById.get(dp.patch_id)
      if (!sp) continue     // race: patch removed server-side
      if (patchesDiffer(dp, sp)) {
        ws.send(JSON.stringify({
          type: 'update_patch',
          data: patchDraftToUpdateWire(dp),
        }))
      }
    }

    // Removed — in server but not in draft → remove_patch
    for (const sp of serverPatches) {
      if (!draftIds.has(sp.patch_id)) {
        ws.send(JSON.stringify({
          type: 'remove_patch',
          data: { patch_id: sp.patch_id },
        }))
      }
    }

    // Optimistic commit. The next weather_state / patches broadcast will
    // correct serverState via syncFromBroadcast (including new patch_ids).
    set((s) => ({ serverState: structuredClone(s.draft) }))
  },

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

  setRunwayFriction: (idx) => set((state) => ({
    draft: {
      ...state.draft,
      global: {
        ...state.draft.global,
        runway_friction: Math.max(0, Math.min(15, Number(idx) || 0)),
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

  // ── Patch actions (Slice 5b-ii — draft only; Accept emits WS messages) ─
  // All mutate draft.patches in-memory; no WS traffic until accept() fires.
  // Per-field overrides (visibility/temperature/precipitation) are gated by
  // explicit override_<field> booleans matching WeatherPatch.msg. Cloud /
  // wind overrides use empty-array-as-inherit convention (no flag).
  addPatch: (role, initial = {}) => set((state) => {
    const patch_type = role === 'custom' ? 'custom' : 'airport'
    const newPatch = {
      client_id: `${role}-${Date.now()}`,
      patch_id:  null,
      role,
      patch_type,
      label:              initial.label || roleToLabel(role),
      icao:               initial.icao || '',
      lat_deg:            initial.lat_deg ?? 0,
      lon_deg:            initial.lon_deg ?? 0,
      ground_elevation_m: initial.ground_elevation_m ?? 0,
      radius_m:           initial.radius_m ?? roleToDefaultRadiusM(role),

      override_visibility:    false,
      visibility_m:           state.draft.global.visibility_m,
      override_temperature:   false,
      temperature_c:          state.draft.global.temperature_c,
      override_precipitation: false,
      precipitation_rate:     state.draft.global.precipitation_rate,
      precipitation_type:     state.draft.global.precipitation_type,

      cloud_layers: [],
      wind_layers:  [],

      // Draft-only / deferred to 5b-iv (not serialized to wire yet)
      override_humidity: false, humidity_pct:     state.draft.global.humidity_pct,
      override_pressure: false, pressure_hpa:     state.draft.global.pressure_hpa,
      override_runway:   false, runway_friction:  state.draft.global.runway_friction ?? 0,

      ...initial,
    }
    return {
      draft: { ...state.draft, patches: [...state.draft.patches, newPatch] },
    }
  }),

  removePatch: (client_id) => set((state) => ({
    draft: {
      ...state.draft,
      patches: state.draft.patches.filter(p => p.client_id !== client_id),
    },
  })),

  updatePatch: (client_id, patch) => set((state) => ({
    draft: {
      ...state.draft,
      patches: state.draft.patches.map(p =>
        p.client_id === client_id ? { ...p, ...patch } : p
      ),
    },
  })),

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
}))
