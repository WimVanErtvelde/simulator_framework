// MSL altitude ↔ pixel-Y scale helpers for WeatherPanelV2 graph column.
//
// NON-LINEAR MAPPING (square root, MAX_FT = 50000):
//   y = height * (1 - sqrt(ft / MAX_FT))
// Gives ~50% of the vertical space to 0-12500 ft (training-relevant) and
// ~22% to 30000-50000 ft (jet cruise — coarse resolution is fine).
//
// Coordinate convention: Y=0 at TOP of graph (max altitude),
// Y=height at BOTTOM (0 ft MSL).

export const MIN_FT = 0
export const MAX_FT = 50000   // matches X-Plane 12 graph range

export const M_TO_FT = 3.28084
export const FT_TO_M = 0.3048

// ft MSL → pixel Y (0 at top, `height` at bottom).
export function ftToY(ft, height) {
  const clamped = Math.max(MIN_FT, Math.min(MAX_FT, ft))
  const frac = Math.sqrt(clamped / MAX_FT)   // 0 at MIN, 1 at MAX
  return height * (1 - frac)
}

// pixel Y → ft MSL (inverse of ftToY).
export function yToFt(y, height) {
  const clamped = Math.max(0, Math.min(height, y))
  const frac = 1 - (clamped / height)        // 0 at bottom, 1 at top
  return frac * frac * MAX_FT
}

// Altitude labels on the axis. Top-down. Uneven due to sqrt mapping —
// we pick these levels so they land at visually-reasonable spacings.
export function axisTicks() {
  return [50000, 40000, 30000, 20000, 10000, 5000, 0]
}
