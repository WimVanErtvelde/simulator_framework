import { useMemo } from 'react'

const SIZE = 180, CX = 90, CY = 90, R = 68

export default function HeadingIndicator({ hdgDeg = 0 }) {
  const ticks = useMemo(() => {
    const t = []
    for (let i = 0; i < 36; i++) {
      const deg = i * 10
      const a = ((deg - 90) * Math.PI) / 180
      const major = deg % 30 === 0
      t.push({
        x1: CX + (R - (major ? 10 : 5)) * Math.cos(a), y1: CY + (R - (major ? 10 : 5)) * Math.sin(a),
        x2: CX + R * Math.cos(a), y2: CY + R * Math.sin(a),
        major,
        label: major ? (deg === 0 ? 'N' : deg === 90 ? 'E' : deg === 180 ? 'S' : deg === 270 ? 'W' : (deg / 10).toString()) : null,
        lx: CX + (R - 18) * Math.cos(a), ly: CY + (R - 18) * Math.sin(a),
      })
    }
    return t
  }, [])

  return (
    <div style={{ textAlign: 'center' }}>
      <svg width={SIZE} height={SIZE} viewBox={`0 0 ${SIZE} ${SIZE}`}>
        <circle cx={CX} cy={CY} r={R + 8} fill="#111827" stroke="#1e293b" strokeWidth={1.5} />
        {/* Rotating compass card */}
        <g transform={`rotate(${-hdgDeg} ${CX} ${CY})`}>
          {ticks.map((t, i) => (
            <g key={i}>
              <line x1={t.x1} y1={t.y1} x2={t.x2} y2={t.y2} stroke="#94a3b8" strokeWidth={t.major ? 1.5 : 0.7} />
              {t.label && (
                <text x={t.lx} y={t.ly} fill={t.label === 'N' ? '#ff3b30' : '#e2e8f0'}
                  fontSize={t.label.length === 1 && 'NESW'.includes(t.label) ? 12 : 10}
                  fontWeight={700} textAnchor="middle" dominantBaseline="central"
                  fontFamily="'JetBrains Mono', monospace"
                  transform={`rotate(${hdgDeg} ${t.lx} ${t.ly})`}>
                  {t.label}
                </text>
              )}
            </g>
          ))}
        </g>
        {/* Fixed lubber line (top) */}
        <polygon points={`${CX},${CY - R - 2} ${CX - 5},${CY - R - 10} ${CX + 5},${CY - R - 10}`}
          fill="#f59e0b" />
        {/* Center airplane symbol */}
        <line x1={CX} y1={CY - 16} x2={CX} y2={CY + 16} stroke="#f59e0b" strokeWidth={2} />
        <line x1={CX - 20} y1={CY} x2={CX + 20} y2={CY} stroke="#f59e0b" strokeWidth={2} />
        <line x1={CX - 8} y1={CY + 12} x2={CX + 8} y2={CY + 12} stroke="#f59e0b" strokeWidth={1.5} />
        {/* Digital readout */}
        <text x={CX} y={CY + 36} fill="#e2e8f0" fontSize={12} fontWeight={600} textAnchor="middle"
          fontFamily="'JetBrains Mono', monospace">{Math.round(hdgDeg).toString().padStart(3, '0')}°</text>
      </svg>
    </div>
  )
}
