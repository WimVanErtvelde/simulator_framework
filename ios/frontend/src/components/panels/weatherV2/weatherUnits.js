// Unit conversion helpers for WeatherPanelV2.
//
// Storage convention (draft + serverState in the store):
//   temperature_c       : °C
//   pressure_hpa        : hPa
//   visibility_m        : meters
//   humidity_pct        : 0-100 %
//   precipitation_rate  : 0.0-1.0
//   precipitation_type  : 0-3 enum
//
// Wire convention (set_weather / weather_state, backend ios_backend_node.py):
//   temperature_sl_k    : Kelvin
//   pressure_sl_pa      : Pascal
//   visibility_m        : meters (same)
//   humidity_pct        : int (same)
//   precipitation_rate  : 0-1 float (same)
//   precipitation_type  : int (same)
//
// Runway condition is NOT part of V2's draft. It flows through the shared
// store (useSimStore.activeWeather.runwayFriction) and fires via
// set_runway_condition on each button press, matching the WX panel.

export const K_OFFSET     = 273.15
export const HPA_TO_PA    = 100.0
export const M_PER_SM     = 1609.344
export const HPA_PER_INHG = 33.8638866

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
// Returns scalar fields only. cloud_layers / wind_layers are owned by later
// slices; callers should spread this into the existing global to preserve
// those arrays.
export function activeWeatherToGlobalDraft(active) {
  return {
    temperature_c:      (active.temperatureSlK ?? 288.15) - K_OFFSET,
    pressure_hpa:       (active.pressureSlPa   ?? 101325) / HPA_TO_PA,
    visibility_m:        active.visibilityM        ?? 160000,
    humidity_pct:        active.humidityPct        ?? 50,
    precipitation_rate:  active.precipitationRate  ?? 0.0,
    precipitation_type:  active.precipitationType  ?? 0,
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
