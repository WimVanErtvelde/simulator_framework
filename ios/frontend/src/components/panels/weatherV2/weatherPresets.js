// Visibility presets for the WeatherPanelV2 Global tab.
//
// Values are training-convention authoring targets, not regulatory minima.
// Clicking a preset fills the slider so the instructor can set up a
// scenario at or below the named category quickly.

export const VISIBILITY_PRESETS = [
  { id: 'cat3',    label: 'CAT III', visibility_m:    75 },
  { id: 'cat2',    label: 'CAT II',  visibility_m:   300 },
  { id: 'cat1',    label: 'CAT I',   visibility_m:   800 },
  { id: 'lvp',     label: 'LVP',     visibility_m:  1500 },
  { id: 'mvfr',    label: 'MVFR',    visibility_m:  3000 },
  { id: 'vfr',     label: 'VFR',     visibility_m:  8000 },
  { id: 'cavok',   label: 'CAVOK',   visibility_m: 10000 },
]

// Precipitation type enum (matches WeatherState.msg)
export const PRECIP_TYPES = [
  { id: 0, label: 'None' },
  { id: 1, label: 'Rain' },
  { id: 2, label: 'Snow' },
  { id: 3, label: 'Sleet' },
]

// Runway friction UI enum (0-2). Wire encoding is 0-15 EURAMEC — see
// weatherUnits.js for the mapping.
export const RUNWAY_FRICTIONS = [
  { id: 0, label: 'Poor' },
  { id: 1, label: 'Fair' },
  { id: 2, label: 'Good' },
]

// Runway conditions — UI-only for now (no wire field yet).
export const RUNWAY_CONDITIONS = [
  { id: 0, label: 'Uniform' },
  { id: 1, label: 'Patchy' },
]
