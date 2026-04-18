import { useMemo } from 'react'

const SIZE = 180, CX = 90, CY = 90, R = 72
// Real C172 VSI: 0 fpm at 9 o'clock. Climb CW (up), descent CCW (down).
// Scale: non-linear. 0-500-1000-1500-2000 fpm on each side.
// Needle sweep: ~150° each side from 9 o'clock center.

// Map fpm to angle in degrees (0° = 3 o'clock in SVG)
// 0 fpm = 180° (9 o'clock)
// +2000 fpm = 0° (3 o'clock via top / CW)
// -2000 fpm = 360° (3 o'clock via bottom / CCW)
function fpmToAngle(fpm) {
  const clamped = Math.max(-2000, Math.min(2000, fpm))
  // Non-linear scale: compress near center, expand near limits
  // Using a simple approach: the first 1000 fpm takes 70°, next 1000 takes 80°
  const sign = clamped >= 0 ? 1 : -1  // climb → angle increases toward 12 o'clock (270°)
  const abs = Math.abs(clamped)
  let offset
  if (abs <= 1000) {
    offset = (abs / 1000) * 80
  } else {
    offset = 80 + ((abs - 1000) / 1000) * 70
  }
  return 180 + sign * offset  // 180° is 9 o'clock
}

export default function VSI({ vsFpm = 0 }) {
  const angle = fpmToAngle(vsFpm)
  const needleRad = (angle * Math.PI) / 180
  const nx = CX + 62 * Math.cos(needleRad)
  const ny = CY + 62 * Math.sin(needleRad)

  const ticks = useMemo(() => {
    const values = [-2000, -1500, -1000, -500, 0, 500, 1000, 1500, 2000]
    return values.map(v => {
      const a = (fpmToAngle(v) * Math.PI) / 180
      const major = Math.abs(v) % 1000 === 0
      return {
        x1: CX + (R - (major ? 8 : 4)) * Math.cos(a), y1: CY + (R - (major ? 8 : 4)) * Math.sin(a),
        x2: CX + R * Math.cos(a), y2: CY + R * Math.sin(a),
        label: major ? (Math.abs(v) / 100).toString() : null,
        lx: CX + (R - 16) * Math.cos(a), ly: CY + (R - 16) * Math.sin(a),
        major,
      }
    })
  }, [])

  return (
    <div style={{ textAlign: 'center' }}>
      <svg width={SIZE} height={SIZE} viewBox={`0 0 ${SIZE} ${SIZE}`}>
        <circle cx={CX} cy={CY} r={R + 6} fill="#111827" stroke="#1e293b" strokeWidth={1.5} />
        {ticks.map((t, i) => (
          <g key={i}>
            <line x1={t.x1} y1={t.y1} x2={t.x2} y2={t.y2} stroke="#94a3b8" strokeWidth={t.major ? 1.5 : 0.8} />
            {t.label && <text x={t.lx} y={t.ly} fill="#94a3b8" fontSize={11} fontWeight={600}
              textAnchor="middle" dominantBaseline="central"
              fontFamily="'JetBrains Mono', monospace">{t.label}</text>}
          </g>
        ))}
        {/* UP / DN labels */}
        <text x={CX} y={CY - 30} fill="#64748b" fontSize={8} textAnchor="middle"
          fontFamily="'JetBrains Mono', monospace">UP</text>
        <text x={CX} y={CY + 34} fill="#64748b" fontSize={8} textAnchor="middle"
          fontFamily="'JetBrains Mono', monospace">DN</text>
        {/* Needle */}
        <line x1={CX} y1={CY} x2={nx} y2={ny} stroke="#e2e8f0" strokeWidth={2} strokeLinecap="round" />
        <circle cx={CX} cy={CY} r={4} fill="#e2e8f0" />
        <text x={CX - 30} y={CY + 2} fill="#64748b" fontSize={7} textAnchor="middle"
          fontFamily="'JetBrains Mono', monospace">VERT SPD</text>
      </svg>
    </div>
  )
}
