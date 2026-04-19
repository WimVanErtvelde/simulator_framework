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
// Runway condition and cloud layers are now part of V2's draft (Slice
// 5a-iii.1). They are committed atomically with atmospheric scalars on
// Accept — not fired per-click. WX's dedicated handlers still exist for
// WX's own UI, which this store does not drive.

export const K_OFFSET     = 273.15
export const HPA_TO_PA    = 100.0
export const M_PER_SM     = 1609.344
export const HPA_PER_INHG = 33.8638866

// ── Store → Wire (set_weather payload) ────────────────────────────────────
// cloud_layers is always included (possibly empty) so an Accept with all
// clouds deleted can clear the backend's cloud list.
export function globalDraftToWire(global) {
  return {
    temperature_sl_k:   global.temperature_c + K_OFFSET,
    pressure_sl_pa:     global.pressure_hpa * HPA_TO_PA,
    visibility_m:       global.visibility_m,
    humidity_pct:       global.humidity_pct,
    precipitation_rate: global.precipitation_rate,
    precipitation_type: global.precipitation_type,
    runway_friction:    global.runway_friction ?? 0,
    cloud_layers: (global.cloud_layers ?? []).map(cl => ({
      cloud_type:   cl.cloud_type,
      base_agl_ft:  cl.base_agl_ft,
      thickness_m:  cl.thickness_m,
      coverage_pct: cl.coverage_pct,
    })),
  }
}

// ── useSimStore.activeWeather (camelCase, post-broadcast-parse) → Store ──
// activeWeather.cloudLayers is a pass-through of the snake-case objects
// the backend publishes: {cloud_type, base_agl_ft, thickness_m,
// coverage_pct}. Re-shape defensively to drop any extra keys.
export function activeWeatherToGlobalDraft(active) {
  const cloud_layers = (active.cloudLayers ?? []).map(cl => ({
    cloud_type:   cl.cloud_type,
    base_agl_ft:  cl.base_agl_ft,
    thickness_m:  cl.thickness_m,
    coverage_pct: cl.coverage_pct,
  }))
  return {
    temperature_c:      (active.temperatureSlK ?? 288.15) - K_OFFSET,
    pressure_hpa:       (active.pressureSlPa   ?? 101325) / HPA_TO_PA,
    visibility_m:        active.visibilityM        ?? 160000,
    humidity_pct:        active.humidityPct        ?? 50,
    precipitation_rate:  active.precipitationRate  ?? 0.0,
    precipitation_type:  active.precipitationType  ?? 0,
    runway_friction:     active.runwayFriction     ?? 0,
    cloud_layers,
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

// ── ISA atmosphere helpers (tropospheric, 0-11 km) ────────────────────────
// Matches the standard lapse X-Plane uses when interpolating SL values to
// altitude. SI-in, SI-out. No store reads.

const ISA_T0_K      = 288.15        // 15 °C
const ISA_P0_PA     = 101325.0
const ISA_LAPSE_K_M = 0.0065        // troposphere lapse (K/m)
const ISA_G         = 9.80665
const ISA_R         = 287.058
const ISA_GEXP      = ISA_G / (ISA_LAPSE_K_M * ISA_R)  // ≈ 5.2561

// Temperature (K) at altMslM meters, starting from slTempK at sea level.
export function isaTemperatureAt(slTempK, altMslM) {
  const h = Math.max(0, Math.min(11000, altMslM))
  return slTempK - ISA_LAPSE_K_M * h
}

// ISA-reference temperature (K) at altMslM meters (for ISA deviation).
export function isaReferenceAt(altMslM) {
  return isaTemperatureAt(ISA_T0_K, altMslM)
}

// Static pressure (Pa) at altMslM meters, starting from slPressurePa at
// sea level, under ISA. Close to (but not identical to) true QNH-based
// altimeter setting; for training display the difference is <1 hPa.
export function isaPressureAt(slPressurePa, altMslM) {
  const h = Math.max(0, Math.min(11000, altMslM))
  const ratio = 1 - (ISA_LAPSE_K_M * h) / ISA_T0_K
  return slPressurePa * Math.pow(ratio, ISA_GEXP)
}

// Keep ISA_P0_PA referenced for future use (prevents unused-var lint).
export const ISA_SEA_LEVEL_PRESSURE_PA = ISA_P0_PA
