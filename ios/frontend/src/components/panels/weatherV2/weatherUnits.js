// Unit conversion helpers for WeatherPanelV2.
//
// Storage convention (draft + serverState in the store):
//   temperature_c       : °C
//   pressure_hpa        : hPa
//   visibility_m        : meters
//   humidity_pct        : 0-100 %
//   precipitation_rate  : 0.0-1.0
//   precipitation_type  : 0-3 enum
//   runway_friction     : 0-2 UI enum (Poor/Fair/Good)
//   runway_conditions   : 0-1 UI enum (Uniform/Patchy, not on wire)
//
// Wire convention (set_weather / weather_state, backend ios_backend_node.py):
//   temperature_sl_k    : Kelvin
//   pressure_sl_pa      : Pascal
//   visibility_m        : meters (same)
//   humidity_pct        : int (same)
//   precipitation_rate  : 0-1 float (same)
//   precipitation_type  : int (same)
//   runway_friction     : 0-15 EURAMEC/TALPA index (different encoding!)

export const K_OFFSET     = 273.15
export const HPA_TO_PA    = 100.0
export const M_PER_SM     = 1609.344
export const HPA_PER_INHG = 33.8638866

// ── Runway friction mapping: UI 0-2 ↔ wire 0-15 ───────────────────────────
// UI is a 3-state simplification of the 0-15 EURAMEC/TALPA scale. Round-trip
// through the wire is lossy for Poor/Fair sub-severity, which is by design —
// V2 chooses representative values.
//
// Wire index breakdown (per old WeatherPanel convention):
//   0        DRY
//   1-3      WET     (1=light, 2=medium, 3=max)
//   4-6      WATER   (4=light, 5=medium, 6=max)
//   7-9      SNOW
//   10-12    ICE
//   13-15    SN+IC
const UI_TO_WIRE_RUNWAY = { 0: 5, 1: 2, 2: 0 }  // Poor→WATER-med, Fair→WET-med, Good→DRY

export function uiToWireRunwayFriction(ui) {
  return UI_TO_WIRE_RUNWAY[ui] ?? 0
}

export function wireToUiRunwayFriction(wire) {
  const w = Number(wire) || 0
  if (w === 0)     return 2     // DRY → Good
  if (w <= 3)      return 1     // WET → Fair
  return 0                      // WATER/SNOW/ICE → Poor
}

// ── Store → Wire (set_weather payload) ────────────────────────────────────
export function globalDraftToWire(global) {
  return {
    temperature_sl_k:   global.temperature_c + K_OFFSET,
    pressure_sl_pa:     global.pressure_hpa * HPA_TO_PA,
    visibility_m:       global.visibility_m,
    humidity_pct:       global.humidity_pct,
    precipitation_rate: global.precipitation_rate,
    precipitation_type: global.precipitation_type,
  }
}

// ── useSimStore.activeWeather (camelCase, post-broadcast-parse) → Store ──
// Input shape comes from useSimStore line ~494-508 after weather_state parse.
// Returns only the scalar fields — cloud_layers / wind_layers are owned by
// later slices and merge-synced separately, so the caller should spread this
// into the existing global to preserve arrays.
export function activeWeatherToGlobalDraft(active) {
  return {
    temperature_c:      (active.temperatureSlK ?? 288.15) - K_OFFSET,
    pressure_hpa:       (active.pressureSlPa   ?? 101325) / HPA_TO_PA,
    visibility_m:        active.visibilityM        ?? 160000,
    humidity_pct:        active.humidityPct        ?? 50,
    precipitation_rate:  active.precipitationRate  ?? 0.0,
    precipitation_type:  active.precipitationType  ?? 0,
    runway_friction:    wireToUiRunwayFriction(active.runwayFriction ?? 0),
    // runway_conditions is UI-only; intentionally not in this object so
    // syncFromBroadcast doesn't overwrite the user's last Uniform/Patchy pick.
  }
}

// ── Display-unit helpers (UI-only, no storage impact) ─────────────────────

export function metersToSM(m)  { return m / M_PER_SM }
export function smToMeters(sm) { return sm * M_PER_SM }

export function formatVisibility(m, unit) {
  if (unit === 'SM') return `${metersToSM(m).toFixed(m < 1609 ? 2 : 1)} SM`
  if (m < 10000)     return `${Math.round(m)} m`
  return `${(m / 1000).toFixed(1)} km`
}

// RVR mode threshold — below this we show finer increments and label "RVR"
export function isRvrMode(m) { return m < 1500 }

export function hpaToInHg(hpa)  { return hpa / HPA_PER_INHG }
export function inHgToHpa(inHg) { return inHg * HPA_PER_INHG }

export function formatPressure(hpa, unit) {
  if (unit === 'inHg') return `${hpaToInHg(hpa).toFixed(2)} inHg`
  return `${hpa.toFixed(1)} hPa`
}
