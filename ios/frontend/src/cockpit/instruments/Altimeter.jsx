import { useMemo } from 'react'

const SIZE = 180, CX = 90, CY = 90, R = 72

function feetToAngle100(ft) { return ((ft % 1000) / 1000) * 360 - 90 }
function feetToAngle1000(ft) { return ((ft % 10000) / 10000) * 360 - 90 }
function feetToAngle10000(ft) { return (Math.min(ft, 30000) / 30000) * 360 - 90 }

function needle(cx, cy, len, angleDeg, width = 2, color = '#e2e8f0') {
  const rad = (angleDeg * Math.PI) / 180
  return <line x1={cx} y1={cy} x2={cx + len * Math.cos(rad)} y2={cy + len * Math.sin(rad)}
    stroke={color} strokeWidth={width} strokeLinecap="round" />
}

export default function Altimeter({ altFt = 0, qnhHpa = 1013.25 }) {
  const ticks = useMemo(() => {
    const t = []
    for (let i = 0; i < 10; i++) {
      const a = ((i / 10) * 360 - 90) * Math.PI / 180
      const major = i % 2 === 0
      t.push({
        x1: CX + (R - (major ? 8 : 4)) * Math.cos(a), y1: CY + (R - (major ? 8 : 4)) * Math.sin(a),
        x2: CX + R * Math.cos(a), y2: CY + R * Math.sin(a),
        label: major ? i.toString() : null,
        lx: CX + (R - 16) * Math.cos(a), ly: CY + (R - 16) * Math.sin(a),
      })
    }
    return t
  }, [])

  return (
    <div style={{ textAlign: 'center' }}>
      <svg width={SIZE} height={SIZE} viewBox={`0 0 ${SIZE} ${SIZE}`}>
        <circle cx={CX} cy={CY} r={R + 6} fill="#111827" stroke="#1e293b" strokeWidth={1.5} />
        {/* Tick marks 0-9 */}
        {ticks.map((t, i) => (
          <g key={i}>
            <line x1={t.x1} y1={t.y1} x2={t.x2} y2={t.y2} stroke="#94a3b8" strokeWidth={t.label ? 1.5 : 0.8} />
            {t.label && <text x={t.lx} y={t.ly} fill="#94a3b8" fontSize={12} fontWeight={600}
              textAnchor="middle" dominantBaseline="central"
              fontFamily="'JetBrains Mono', monospace">{t.label}</text>}
          </g>
        ))}
        {/* Kollsman window (QNH) */}
        <rect x={CX + 16} y={CY - 10} width={36} height={18} rx={2}
          fill="#0a0e14" stroke="#1e293b" strokeWidth={1} />
        <text x={CX + 34} y={CY + 1} fill="#e2e8f0" fontSize={9} textAnchor="middle"
          dominantBaseline="central" fontFamily="'JetBrains Mono', monospace">
          {qnhHpa.toFixed(0)}
        </text>
        {/* 10000ft needle (short, triangle) */}
        {needle(CX, CY, 32, feetToAngle10000(altFt), 3, '#94a3b8')}
        {/* 1000ft needle (medium) */}
        {needle(CX, CY, 52, feetToAngle1000(altFt), 2.5, '#e2e8f0')}
        {/* 100ft needle (long, thin) */}
        {needle(CX, CY, 64, feetToAngle100(altFt), 1.5, '#e2e8f0')}
        <circle cx={CX} cy={CY} r={4} fill="#e2e8f0" />
        {/* Digital readout */}
        <text x={CX} y={CY + 30} fill="#64748b" fontSize={9} textAnchor="middle"
          fontFamily="'JetBrains Mono', monospace">ALT FT</text>
      </svg>
    </div>
  )
}
