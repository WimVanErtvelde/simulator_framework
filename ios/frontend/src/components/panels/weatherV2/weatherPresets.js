// Visibility presets for the WeatherPanelV2 Global tab.
//
// Values are training-convention authoring targets, not regulatory minima.
// Clicking a preset fills the slider so the instructor can set up a
// scenario at or below the named category quickly.

export const VISIBILITY_PRESETS = [
  { id: 'cat3',  label: 'CAT III', visibility_m:    75 },
  { id: 'cat2',  label: 'CAT II',  visibility_m:   300 },
  { id: 'cat1',  label: 'CAT I',   visibility_m:   800 },
  { id: 'lvp',   label: 'LVP',     visibility_m:  1500 },
  { id: 'mvfr',  label: 'MVFR',    visibility_m:  3000 },
  { id: 'vfr',   label: 'VFR',     visibility_m:  8000 },
  { id: 'cavok', label: 'CAVOK',   visibility_m: 10000 },
]

// Precipitation type enum (matches WeatherState.msg)
export const PRECIP_TYPES = [
  { id: 0, label: 'None' },
  { id: 1, label: 'Rain' },
  { id: 2, label: 'Snow' },
  { id: 3, label: 'Sleet' },
]

// CIGI cloud types with V2 display labels + band colors.
// Colors are chosen to hint the severity of the layer (pink/Cb stands out).
export const CLOUD_TYPES = [
  { id: 5,  label: 'Cirrus',        color: '#94a3b8' },
  { id: 10, label: 'Stratus',       color: '#71717a' },
  { id: 7,  label: 'Cumulus',       color: '#39d0d8' },
  { id: 6,  label: 'Cumulonimbus',  color: '#bc4fcb' },
  { id: 8,  label: 'Nimbostratus',  color: '#3b82f6' },
  { id: 9,  label: 'Stratocumulus', color: '#64748b' },
]

export function cloudTypeInfo(id) {
  return CLOUD_TYPES.find(t => t.id === id) ?? { id, label: 'Cloud', color: '#39d0d8' }
}

// Runway condition model is NOT here — V2 uses the WX panel's 6-category ×
// 3-severity index (0-15) directly via set_runway_condition. See RunwayField.
