import { useMemo } from 'react'

const SIZE = 160
const CX = SIZE / 2, CY = SIZE / 2, R = 60
const START_ANGLE = -225, END_ANGLE = 45  // 270-degree sweep

function valToAngle(val, min, max) {
  const pct = Math.max(0, Math.min(1, (val - min) / (max - min)))
  return START_ANGLE + pct * (END_ANGLE - START_ANGLE)
}

function arcPath(cx, cy, r, startDeg, endDeg) {
  const s = (startDeg * Math.PI) / 180
  const e = (endDeg * Math.PI) / 180
  const x1 = cx + r * Math.cos(s), y1 = cy + r * Math.sin(s)
  const x2 = cx + r * Math.cos(e), y2 = cy + r * Math.sin(e)
  const large = endDeg - startDeg > 180 ? 1 : 0
  return `M ${x1} ${y1} A ${r} ${r} 0 ${large} 1 ${x2} ${y2}`
}

export default function RoundGauge({
  value = 0, min = 0, max = 100, label = '', unit = '',
  greenArc, yellowArc, redLine, redArc,
  ticks = 5, decimals = 0, size = SIZE,
}) {
  const scale = size / SIZE
  const angle = valToAngle(value, min, max)
  const needleLen = R - 4

  const arcs = useMemo(() => {
    const result = []
    const makeArc = (lo, hi, color, width = 4) => {
      const a1 = valToAngle(lo, min, max)
      const a2 = valToAngle(hi, min, max)
      result.push({ d: arcPath(CX, CY, R - 6, a1, a2), color, width })
    }
    if (greenArc) makeArc(greenArc[0], greenArc[1], '#22c55e')
    if (yellowArc) makeArc(yellowArc[0], yellowArc[1], '#eab308')
    if (redArc) makeArc(redArc[0], redArc[1], '#ef4444')
    if (redLine != null) {
      const a = valToAngle(redLine, min, max)
      const rad = (a * Math.PI) / 180
      result.push({
        line: true,
        x1: CX + (R - 12) * Math.cos(rad), y1: CY + (R - 12) * Math.sin(rad),
        x2: CX + R * Math.cos(rad), y2: CY + R * Math.sin(rad),
        color: '#ef4444',
      })
    }
    return result
  }, [min, max, greenArc, yellowArc, redArc, redLine])

  const tickMarks = useMemo(() => {
    const marks = []
    for (let i = 0; i <= ticks; i++) {
      const v = min + (i / ticks) * (max - min)
      const a = (valToAngle(v, min, max) * Math.PI) / 180
      marks.push({
        x1: CX + (R - 2) * Math.cos(a), y1: CY + (R - 2) * Math.sin(a),
        x2: CX + (R + 4) * Math.cos(a), y2: CY + (R + 4) * Math.sin(a),
        label: v % 1 === 0 ? v.toString() : v.toFixed(1),
        lx: CX + (R + 14) * Math.cos(a), ly: CY + (R + 14) * Math.sin(a),
      })
    }
    return marks
  }, [min, max, ticks])

  const needleRad = (angle * Math.PI) / 180
  const nx = CX + needleLen * Math.cos(needleRad)
  const ny = CY + needleLen * Math.sin(needleRad)

  return (
    <div style={{ width: size, textAlign: 'center' }}>
      <svg width={size} height={size} viewBox={`0 0 ${SIZE} ${SIZE}`}>
        {/* Background */}
        <circle cx={CX} cy={CY} r={R + 8} fill="#111827" stroke="#1e293b" strokeWidth={1.5} />

        {/* Tick marks */}
        {tickMarks.map((t, i) => (
          <g key={i}>
            <line x1={t.x1} y1={t.y1} x2={t.x2} y2={t.y2} stroke="#64748b" strokeWidth={1} />
            <text x={t.lx} y={t.ly} fill="#64748b" fontSize={8} textAnchor="middle" dominantBaseline="central">
              {t.label}
            </text>
          </g>
        ))}

        {/* Color arcs */}
        {arcs.map((a, i) =>
          a.line ? (
            <line key={i} x1={a.x1} y1={a.y1} x2={a.x2} y2={a.y2} stroke={a.color} strokeWidth={2.5} />
          ) : (
            <path key={i} d={a.d} fill="none" stroke={a.color} strokeWidth={a.width} strokeLinecap="round" />
          )
        )}

        {/* Needle */}
        <line x1={CX} y1={CY} x2={nx} y2={ny} stroke="#ff3b30" strokeWidth={2} strokeLinecap="round" />
        <circle cx={CX} cy={CY} r={4} fill="#ff3b30" />

        {/* Digital readout */}
        <text x={CX} y={CY + 24} fill="#e2e8f0" fontSize={14} fontWeight={600}
          textAnchor="middle" fontFamily="'JetBrains Mono', monospace">
          {value.toFixed(decimals)}
        </text>
        {unit && (
          <text x={CX} y={CY + 36} fill="#64748b" fontSize={9} textAnchor="middle"
            fontFamily="'JetBrains Mono', monospace">
            {unit}
          </text>
        )}
      </svg>
      <div style={{ color: '#94a3b8', fontSize: 10, fontFamily: "'JetBrains Mono', monospace", marginTop: -4 }}>
        {label}
      </div>
    </div>
  )
}
