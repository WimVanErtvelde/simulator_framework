import { create } from 'zustand'
import { useSimStore } from './useSimStore'
import {
  globalDraftToWire,
  activeWeatherToGlobalDraft,
  uiToWireRunwayFriction,
} from '../components/panels/weatherV2/weatherUnits'

// Draft state for WeatherPanelV2.
//
// Session-volatile: lives only in React memory. serverState mirrors the last
// accepted snapshot (optimistically updated on Accept, then corrected by the
// next /world/weather broadcast via syncFromBroadcast). draft !== serverState
// drives the dirty indicator.

const initialDraft = {
  global: {
    temperature_c:        15.0,
    pressure_hpa:         1013.25,
    visibility_m:         160000,
    humidity_pct:         50,
    precipitation_rate:   0.0,
    precipitation_type:   0,
    runway_friction:      2,    // UI enum 0-2 (Poor/Fair/Good), not wire 0-15
    runway_conditions:    0,    // UI enum (Uniform/Patchy) — no wire field yet
    cloud_layers:         [],   // Slice 5a-iii
    wind_layers:          [],   // Slice 5a-iv
  },
  patches: [],
  microbursts: [],
}

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

  // Commits the draft to the backend via the existing set_weather handler
  // (ios_backend_node.py:2526). Runway friction goes separately via the
  // existing set_runway_condition handler because the wire uses a 0-15
  // EURAMEC index and the UI stores a 0-2 enum.
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

    if (state.draft.global.runway_friction !== state.serverState.global.runway_friction) {
      ws.send(JSON.stringify({
        type: 'set_runway_condition',
        data: { index: uiToWireRunwayFriction(state.draft.global.runway_friction) },
      }))
    }

    // Optimistic commit. A subsequent weather_state broadcast will correct
    // serverState via syncFromBroadcast if the backend clamped or rejected
    // any field.
    set((s) => ({ serverState: structuredClone(s.draft) }))
  },

  discard: () => set((state) => ({ draft: structuredClone(state.serverState) })),
}))
