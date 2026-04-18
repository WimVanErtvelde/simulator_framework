import { create } from 'zustand'

// Draft state for WeatherPanelV2.
//
// Session-volatile: lives only in React memory. Committing via Accept sends to
// ios_backend (Slice 5a-ii+). serverState mirrors the last accepted snapshot,
// so `draft !== serverState` drives the dirty indicator.
//
// Slice 5a-i: only `units` and `activeTab` have real behavior. draft fields
// exist so dirty-state wiring can be demonstrated end-to-end, but no tab
// actually mutates them yet.

const initialDraft = {
  global: {
    temperature_c:        15.0,
    pressure_hpa:         1013.25,
    visibility_m:         160000,
    humidity_pct:         50,
    precipitation_rate:   0.0,
    precipitation_type:   0,
    runway_friction:      2,
    runway_conditions:    0,
    wave_height_m:        0.0,
    wave_direction_deg:   0.0,
    cloud_layers:         [],
    wind_layers:          [],
  },
  patches: [],
  microbursts: [],
}

export const useWeatherV2Store = create((set) => ({
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

  // Slice 5a-i: no-op placeholder — copies draft → serverState so dirty-state
  // wiring can be tested. Slice 5a-ii wires real WebSocket messages.
  accept: () => {
    console.log('[WeatherV2] accept() — not yet wired to backend (Slice 5a-ii)')
    set((state) => ({ serverState: structuredClone(state.draft) }))
  },

  discard: () => set((state) => ({ draft: structuredClone(state.serverState) })),
}))
