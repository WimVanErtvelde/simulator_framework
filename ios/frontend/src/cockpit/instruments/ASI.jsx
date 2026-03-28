import { useMemo } from 'react'

const SIZE = 180, CX = 90, CY = 90, R = 72
// C172S V-speeds (KIAS)
const VS0 = 40, VS1 = 48, VFE = 110, VNO = 129, VNE = 163
const MIN_KT = 0, MAX_KT = 200
const START = -225, END = 45 // 270-degree sweep

function ktToAngle(kt) {
  // Non-linear: 0-40 is compressed, 40-200 is the main range
  const usable = Math.max(0, Math.min(MAX_KT, kt))
  if (usable <= 40) return START + (usable / 40) * 30 // first 30 degrees for 0-40kt
  const pct = (usable - 40) / (MAX_KT - 40)
  return START + 30 + pct * 240
}

function arc(r, startKt, endKt, color, width = 5) {
  const a1 = (ktToAngle(startKt) * Math.PI) / 180
  const a2 = (ktToAngle(endKt) * Math.PI) / 180
  const x1 = CX + r * Math.cos(a1), y1 = CY + r * Math.sin(a1)
  const x2 = CX + r * Math.cos(a2), y2 = CY + r * Math.sin(a2)
  const sweep = endKt - startKt
  const large = ktToAngle(endKt) - ktToAngle(startKt) > 180 ? 1 : 0
  return <path d={`M ${x1} ${y1} A ${r} ${r} 0 ${large} 1 ${x2} ${y2}`}
    fill="none" stroke={color} strokeWidth={width} strokeLinecap="round" />
}

export default function ASI({ iasKt = 0 }) {
  const angle = ktToAngle(iasKt)
  const needleRad = (angle * Math.PI) / 180
  const nx = CX + 62 * Math.cos(needleRad)
  const ny = CY + 62 * Math.sin(needleRad)

  const ticks = useMemo(() => {
    const t = []
    for (let kt = 40; kt <= 200; kt += 20) {
      const a = (ktToAngle(kt) * Math.PI) / 180
      t.push({
        x1: CX + (R - 2) * Math.cos(a), y1: CY + (R - 2) * Math.sin(a),
        x2: CX + (R + 3) * Math.cos(a), y2: CY + (R + 3) * Math.sin(a),
        lx: CX + (R - 14) * Math.cos(a), ly: CY + (R - 14) * Math.sin(a),
        label: kt.toString(),
      })
    }
    return t
  }, [])

  return (
    <div style={{ textAlign: 'center' }}>
      <svg width={SIZE} height={SIZE} viewBox={`0 0 ${SIZE} ${SIZE}`}>
        <circle cx={CX} cy={CY} r={R + 6} fill="#111827" stroke="#1e293b" strokeWidth={1.5} />
        {/* Color arcs */}
        {arc(R - 6, VS0, VFE, '#ffffff', 4)}    {/* White: Vs0-Vfe (flaps operating) */}
        {arc(R - 6, VS1, VNO, '#22c55e', 4)}    {/* Green: Vs1-Vno (normal) */}
        {arc(R - 6, VNO, VNE, '#eab308', 4)}    {/* Yellow: Vno-Vne (caution) */}
        {/* Vne red line */}
        {(() => {
          const a = (ktToAngle(VNE) * Math.PI) / 180
          return <line x1={CX + (R - 12) * Math.cos(a)} y1={CY + (R - 12) * Math.sin(a)}
            x2={CX + R * Math.cos(a)} y2={CY + R * Math.sin(a)} stroke="#ef4444" strokeWidth={3} />
        })()}
        {/* Ticks */}
        {ticks.map((t, i) => (
          <g key={i}>
            <line x1={t.x1} y1={t.y1} x2={t.x2} y2={t.y2} stroke="#94a3b8" strokeWidth={1} />
            <text x={t.lx} y={t.ly} fill="#94a3b8" fontSize={9} textAnchor="middle" dominantBaseline="central"
              fontFamily="'JetBrains Mono', monospace">{t.label}</text>
          </g>
        ))}
        {/* Needle */}
        <line x1={CX} y1={CY} x2={nx} y2={ny} stroke="#ff3b30" strokeWidth={2} strokeLinecap="round" />
        <circle cx={CX} cy={CY} r={4} fill="#ff3b30" />
        {/* Label */}
        <text x={CX} y={CY + 28} fill="#64748b" fontSize={9} textAnchor="middle"
          fontFamily="'JetBrains Mono', monospace">AIRSPEED KTS</text>
      </svg>
    </div>
  )
}
