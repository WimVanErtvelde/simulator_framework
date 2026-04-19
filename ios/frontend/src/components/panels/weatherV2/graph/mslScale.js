// MSL altitude ↔ pixel-Y scale helpers for WeatherPanelV2 graph column.
//
// Coordinate convention: Y=0 at TOP of graph (max altitude), Y=height at
// BOTTOM of graph (0 ft MSL). Highest altitude drawn at top of viewport.

export const MIN_FT = 0
export const MAX_FT = 50000   // matches X-Plane 12 graph range

export const M_TO_FT = 3.28084
export const FT_TO_M = 0.3048

// ft MSL → pixel Y (0 at top, `height` at bottom).
export function ftToY(ft, height) {
  const clamped = Math.max(MIN_FT, Math.min(MAX_FT, ft))
  const frac = (clamped - MIN_FT) / (MAX_FT - MIN_FT)
  return height - frac * height
}

// pixel Y → ft MSL (0 at top, `height` at bottom).
export function yToFt(y, height) {
  const clamped = Math.max(0, Math.min(height, y))
  const frac = 1 - (clamped / height)
  return MIN_FT + frac * (MAX_FT - MIN_FT)
}

// Feet per pixel at a given viewport height — for drag-delta conversion.
export function ftPerPx(height) {
  return (MAX_FT - MIN_FT) / height
}

// Generate axis tick altitudes. 5000 ft spacing by default (top-down).
export function axisTicks(stepFt = 5000) {
  const ticks = []
  for (let alt = MAX_FT; alt >= MIN_FT; alt -= stepFt) ticks.push(alt)
  return ticks
}
