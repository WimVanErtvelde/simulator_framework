import { create } from 'zustand'
import { useSimStore } from './useSimStore'
import {
  globalDraftToWire,
  activeWeatherToGlobalDraft,
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

function draftEquals(a, b) {
  return JSON.stringify(a) === JSON.stringify(b)
}

export const useWeatherV2Store = create((set, get) => ({
  draft:       structuredClone(initialDraft),
  serverState: structuredClone(initialDraft),

  // Note: no global unit toggle. Aviation convention uses fixed units per
  // field (ft altitudes, kt speeds, °C temperature, hPa pressure). Per-field
  // inline toggles for hPa/inHg, m/SM visibility, etc. land in the slices
  // that introduce those fields.
  activeTab: 'global',

  setActiveTab: (tab) => set({ activeTab: tab }),

  // Generic draft mutator. Tabs call with a path into the draft:
  //   updateDraft(['global', 'temperature_c'], 20)
  updateDraft: (path, value) => set((state) => {
    const next = structuredClone(state.draft)
    let cursor = next
    for (let i = 0; i < path.length - 1; i++) cursor = cursor[path[i]]
    cursor[path[path.length - 1]] = value
    return { draft: next }
  }),

  // Called when useSimStore.activeWeather changes (a weather_state broadcast
  // arrived). Merges scalar fields into serverState.global, and into draft
  // only if draft was clean at the moment of sync — never clobbers in-flight
  // user edits. cloud_layers / wind_layers are not touched here; their sync
  // lands in Slices 5a-iii / 5a-iv.
  syncFromBroadcast: (activeWeather) => set((state) => {
    const fresh = activeWeatherToGlobalDraft(activeWeather)
    const wasClean = draftEquals(state.draft, state.serverState)
    const nextServer = {
      ...state.serverState,
      global: { ...state.serverState.global, ...fresh },
    }
    const nextDraft = wasClean
      ? { ...state.draft, global: { ...state.draft.global, ...fresh } }
      : state.draft
    return { serverState: nextServer, draft: nextDraft }
  }),

  // Commits the draft to the backend via the existing set_weather handler.
  // Runway condition is immediate-apply from RunwayField (not part of this
  // batch).
  accept: () => {
    const state = get()
    if (draftEquals(state.draft, state.serverState)) return

    const { ws, wsConnected } = useSimStore.getState()
    if (!wsConnected || !ws) {
      console.warn('[WeatherV2] accept(): WS not connected — commit dropped')
      return
    }

    ws.send(JSON.stringify({
      type: 'set_weather',
      data: globalDraftToWire(state.draft.global),
    }))

    // Optimistic commit. A subsequent weather_state broadcast will correct
    // serverState via syncFromBroadcast if the backend clamped any field.
    set((s) => ({ serverState: structuredClone(s.draft) }))
  },

  discard: () => set((state) => ({ draft: structuredClone(state.serverState) })),

  // ── Layer selection (drives LayerPropertiesColumn content) ─────────────
  // null when nothing selected; empty-state shown.
  selectedLayer: null,   // { kind: 'cloud' | 'wind', index: number } | null

  selectLayer:   (kind, index) => set({ selectedLayer: { kind, index } }),
  clearSelection: ()           => set({ selectedLayer: null }),

  // ── Cloud layer actions (draft only — committed on Accept) ─────────────
  // All mutate draft.global.cloud_layers in-memory; no WS traffic until
  // accept() fires. Cap at 3 (backend enforces same limit).
  addCloud: () => set((state) => {
    const cur = state.draft.global.cloud_layers ?? []
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
    return {
      draft: {
        ...state.draft,
        global: {
          ...state.draft.global,
          cloud_layers: [...cur, newLayer],
        },
      },
    }
  }),

  removeCloud: (index) => set((state) => {
    const cur = state.draft.global.cloud_layers ?? []
    const next = cur.filter((_, i) => i !== index)
    // Clear selection if the removed layer was selected; shift index down
    // if a higher-index layer was selected.
    let nextSel = state.selectedLayer
    if (nextSel?.kind === 'cloud') {
      if (nextSel.index === index)      nextSel = null
      else if (nextSel.index > index)   nextSel = { ...nextSel, index: nextSel.index - 1 }
    }
    return {
      draft: {
        ...state.draft,
        global: { ...state.draft.global, cloud_layers: next },
      },
      selectedLayer: nextSel,
    }
  }),

  updateCloud: (index, patch) => set((state) => {
    const cur = state.draft.global.cloud_layers ?? []
    if (index < 0 || index >= cur.length) return state
    const next = cur.map((cl, i) => (i === index ? { ...cl, ...patch } : cl))
    return {
      draft: {
        ...state.draft,
        global: { ...state.draft.global, cloud_layers: next },
      },
    }
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
}))
