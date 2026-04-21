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

const FT_TO_M = 0.3048
const M_TO_FT = 1 / FT_TO_M

// ── Store → Wire (set_weather payload) ────────────────────────────────────
// cloud_layers is always included (possibly empty) so an Accept with all
// clouds deleted can clear the backend's cloud list.
//
// AGL/MSL handoff (Slice 5d): the DRAFT carries base_agl_ft (display
// convenience — "above the ground under me"). The WIRE carries
// base_elevation_m (MSL, canonical). Conversion uses aircraft ground
// elevation at the moment of Accept. Caller passes aircraftGroundM.
// Backend no longer maintains a weather_station reference for AGL math.
export function globalDraftToWire(global, aircraftGroundM = 0) {
  return {
    // Every scalar coerces through ?? so a null draft field never reaches
    // the wire — backend int(data.get(k, default)) crashes on explicit null.
    temperature_sl_k:   (global.temperature_c ?? 15.0) + K_OFFSET,
    pressure_sl_pa:     (global.pressure_hpa ?? 1013.25) * HPA_TO_PA,
    visibility_m:       global.visibility_m       ?? 160000,
    humidity_pct:       global.humidity_pct       ?? 50,
    precipitation_rate: global.precipitation_rate ?? 0.0,
    precipitation_type: global.precipitation_type ?? 0,
    runway_friction:    global.runway_friction    ?? 0,
    cloud_layers: (global.cloud_layers ?? []).map(cl => ({
      cloud_type:   cl.cloud_type,
      // Resolve AGL (draft) → MSL (wire) using aircraft ground snapshot.
      base_elevation_m: (cl.base_agl_ft ?? 0) * FT_TO_M + aircraftGroundM,
      thickness_m:  cl.thickness_m,
      coverage_pct: cl.coverage_pct,
    })),
    wind_layers: (global.wind_layers ?? []).map(wl => ({
      altitude_msl_m:       wl.altitude_msl_m,
      wind_direction_deg:   wl.wind_direction_deg,
      wind_speed_ms:        wl.wind_speed_ms,
      vertical_wind_ms:     wl.vertical_wind_ms     ?? 0,
      gust_speed_ms:        wl.gust_speed_ms        ?? 0,
      shear_direction_deg:  wl.shear_direction_deg  ?? 0,
      shear_speed_ms:       wl.shear_speed_ms       ?? 0,
      turbulence_severity:  wl.turbulence_severity  ?? 0,
    })),
  }
}

// ── useSimStore.activeWeather (camelCase, post-broadcast-parse) → Store ──
// Symmetric with globalDraftToWire: broadcast carries base_elevation_m
// (MSL), we reconstruct base_agl_ft relative to current aircraft ground
// for the draft's AGL-oriented display. See AGL/MSL handoff note above.
export function activeWeatherToGlobalDraft(active, aircraftGroundM = 0) {
  const aircraftGroundFt = aircraftGroundM * M_TO_FT
  const cloud_layers = (active.cloudLayers ?? []).map(cl => ({
    cloud_type:   cl.cloud_type,
    base_agl_ft:  (cl.base_elevation_m ?? 0) * M_TO_FT - aircraftGroundFt,
    thickness_m:  cl.thickness_m,
    coverage_pct: cl.coverage_pct,
  }))
  const wind_layers = (active.windLayers ?? []).map(wl => ({
    altitude_msl_m:       wl.altitude_msl_m       ?? 0,
    wind_direction_deg:   wl.wind_direction_deg   ?? 0,
    wind_speed_ms:        wl.wind_speed_ms        ?? 0,
    vertical_wind_ms:     wl.vertical_wind_ms     ?? 0,
    gust_speed_ms:        wl.gust_speed_ms        ?? 0,
    shear_direction_deg:  wl.shear_direction_deg  ?? 0,
    shear_speed_ms:       wl.shear_speed_ms       ?? 0,
    turbulence_severity:  wl.turbulence_severity  ?? 0,
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
    wind_layers,
  }
}

// ── Patch wire builders (Slice 5b-ii / 5b-iv) ─────────────────────────────
// Accept emits per-patch WS messages (add_patch / update_patch). Wire
// shape mirrors WeatherPatch.msg's per-field override model:
//   override_<field> boolean + <field> value, always present together.
// cloud_layers / wind_layers use empty-array-as-inherit convention.

function applyOverridesToWire(p, data) {
  // Every scalar coerces through ?? so a null draft field never reaches
  // the wire — backend int(data.get(k, default)) crashes on explicit null.
  data.override_visibility = !!p.override_visibility
  data.visibility_m        = p.visibility_m ?? 9999

  data.override_temperature = !!p.override_temperature
  data.temperature_k        = (p.temperature_c ?? 15.0) + K_OFFSET

  data.override_precipitation = !!p.override_precipitation
  data.precipitation_rate     = p.precipitation_rate ?? 0.0
  data.precipitation_type     = p.precipitation_type ?? 0

  data.override_humidity = !!p.override_humidity
  data.humidity_pct      = p.humidity_pct ?? 50

  data.override_pressure = !!p.override_pressure
  data.pressure_sl_pa    = (p.pressure_hpa ?? 1013.25) * HPA_TO_PA

  data.override_runway   = !!p.override_runway
  data.runway_friction   = p.runway_friction ?? 0

  // AGL→MSL using this patch's own ground elevation (from airport DB
  // when patch is an airport; SRTM probe for custom). NaN-safe coalesce:
  // SRTM probe failures return NaN on the wire, and `??` does NOT coalesce
  // NaN — it only handles null/undefined. Without this guard, NaN would
  // propagate into cloud base_elevation_m and poison weather_solver.
  const patchGroundM = Number.isFinite(p.ground_elevation_m) ? p.ground_elevation_m : 0
  data.cloud_layers = (p.cloud_layers || []).map(cl => ({
    cloud_type:   cl.cloud_type,
    base_elevation_m: (cl.base_agl_ft ?? 0) * FT_TO_M + patchGroundM,
    thickness_m:  cl.thickness_m,
    coverage_pct: cl.coverage_pct,
  }))
  data.wind_layers = (p.wind_layers || []).map(wl => ({
    altitude_msl_m:       wl.altitude_msl_m,
    wind_direction_deg:   wl.wind_direction_deg,
    wind_speed_ms:        wl.wind_speed_ms,
    vertical_wind_ms:     wl.vertical_wind_ms     ?? 0,
    gust_speed_ms:        wl.gust_speed_ms        ?? 0,
    shear_direction_deg:  wl.shear_direction_deg  ?? 0,
    shear_speed_ms:       wl.shear_speed_ms       ?? 0,
    turbulence_severity:  wl.turbulence_severity  ?? 0,
  }))
}

// ── Patch wire payloads (Slice 5c-refactor-I) ─────────────────────────────
// reserve_patch and update_patch_identity carry only identity fields.
// update_patch_overrides carries only override flags/values + layers.

export function patchIdentityToWire(p) {
  return {
    client_id:          p.client_id,
    patch_type:         p.patch_type,
    role:               p.role,
    label:              p.label,
    icao:               p.icao,
    lat_deg:            p.lat_deg,
    lon_deg:            p.lon_deg,
    radius_m:           p.radius_m,
    ground_elevation_m: p.ground_elevation_m,
  }
}

export function patchOverridesToWire(p) {
  const data = { patch_id: p.patch_id }
  applyOverridesToWire(p, data)
  return data
}

// Reconstruct draft patch shape from the 'patches' WS broadcast.
// Role fallback: if backend hasn't sent role, infer from patch_type
// (airport→departure, custom→custom). Instructor can rename in UI.
export function patchesFromBroadcast(raw) {
  const list = Array.isArray(raw) ? raw : []
  // NaN-safe coalesce for ground_elevation_m: backend returns NaN on SRTM
  // probe failure / timeout / service-not-ready. `??` only handles
  // null/undefined, so guard explicitly.
  const finiteOr = (v, d) => (Number.isFinite(v) ? v : d)
  return list.map(rp => ({
    // Use backend's echoed client_id when present (5c-refactor-I reserve
    // path). Fall back to srv-<id> for legacy patches created pre-refactor
    // where the backend didn't thread client_id (add_patch / scenario load).
    client_id:          rp.client_id || `srv-${rp.patch_id}`,
    patch_id:           rp.patch_id,
    role:               rp.role || (rp.patch_type === 'airport' ? 'departure' : 'custom'),
    patch_type:         rp.patch_type || 'custom',
    label:              rp.label || '',
    icao:               rp.icao  || '',
    lat_deg:            rp.lat_deg ?? 0,
    lon_deg:            rp.lon_deg ?? 0,
    ground_elevation_m: finiteOr(rp.ground_elevation_m, 0),
    radius_m:           rp.radius_m ?? 0,

    override_visibility:    !!rp.override_visibility,
    visibility_m:           rp.visibility_m ?? 9999,
    override_temperature:   !!rp.override_temperature,
    temperature_c:          (rp.temperature_k ?? 288.15) - K_OFFSET,
    override_precipitation: !!rp.override_precipitation,
    precipitation_rate:     rp.precipitation_rate ?? 0,
    precipitation_type:     rp.precipitation_type ?? 0,
    override_humidity:      !!rp.override_humidity,
    humidity_pct:           rp.humidity_pct ?? 50,
    override_pressure:      !!rp.override_pressure,
    pressure_hpa:           (rp.pressure_sl_pa ?? 101325) / HPA_TO_PA,
    override_runway:        !!rp.override_runway,
    runway_friction:        rp.runway_friction ?? 0,

    // MSL→AGL using the patch's own ground elevation, symmetric with
    // applyOverridesToWire. Patches are self-referential for AGL — the
    // patch's ground_elevation_m is the stable reference.
    cloud_layers: (rp.cloud_layers || []).map(cl => ({
      cloud_type:   cl.cloud_type,
      base_agl_ft:  (cl.base_elevation_m ?? 0) * M_TO_FT - (rp.ground_elevation_m ?? 0) * M_TO_FT,
      thickness_m:  cl.thickness_m,
      coverage_pct: cl.coverage_pct,
    })),
    wind_layers:  rp.wind_layers  || [],
  }))
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
