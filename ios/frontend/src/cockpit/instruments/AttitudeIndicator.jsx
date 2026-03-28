const SIZE = 180, CX = 90, CY = 90, R = 72
const DEG_TO_PX = 3 // pixels per degree of pitch

export default function AttitudeIndicator({ pitchDeg = 0, rollDeg = 0 }) {
  const pitchPx = pitchDeg * DEG_TO_PX
  const bankMarks = [10, 20, 30, 60]

  return (
    <div style={{ textAlign: 'center' }}>
      <svg width={SIZE} height={SIZE} viewBox={`0 0 ${SIZE} ${SIZE}`}>
        <defs>
          <clipPath id="ai-clip"><circle cx={CX} cy={CY} r={R} /></clipPath>
        </defs>
        <circle cx={CX} cy={CY} r={R + 6} fill="#111827" stroke="#1e293b" strokeWidth={1.5} />

        {/* Sky/ground rotated by roll, shifted by pitch */}
        <g clipPath="url(#ai-clip)">
          <g transform={`rotate(${-rollDeg} ${CX} ${CY})`}>
            {/* Sky */}
            <rect x={0} y={CY - 200 + pitchPx} width={SIZE} height={200} fill="#1e3a5f" />
            {/* Ground */}
            <rect x={0} y={CY + pitchPx} width={SIZE} height={200} fill="#5c3a1a" />
            {/* Horizon line */}
            <line x1={0} y1={CY + pitchPx} x2={SIZE} y2={CY + pitchPx}
              stroke="#e2e8f0" strokeWidth={1.5} />
            {/* Pitch ladder */}
            {[-20, -15, -10, -5, 5, 10, 15, 20].map(deg => {
              const y = CY + pitchPx - deg * DEG_TO_PX
              const w = deg % 10 === 0 ? 30 : 16
              return (
                <g key={deg}>
                  <line x1={CX - w} y1={y} x2={CX + w} y2={y}
                    stroke="#e2e8f0" strokeWidth={deg % 10 === 0 ? 1 : 0.5} />
                  {deg % 10 === 0 && (
                    <text x={CX + w + 6} y={y} fill="#e2e8f0" fontSize={7}
                      dominantBaseline="central" fontFamily="'JetBrains Mono', monospace">
                      {Math.abs(deg)}
                    </text>
                  )}
                </g>
              )
            })}
          </g>
        </g>

        {/* Bank angle marks (fixed to frame) */}
        {bankMarks.map(deg => {
          const a1 = ((-90 - deg) * Math.PI) / 180
          const a2 = ((-90 + deg) * Math.PI) / 180
          return [a1, a2].map((a, i) => (
            <line key={`${deg}-${i}`}
              x1={CX + (R - 2) * Math.cos(a)} y1={CY + (R - 2) * Math.sin(a)}
              x2={CX + (R + 5) * Math.cos(a)} y2={CY + (R + 5) * Math.sin(a)}
              stroke="#94a3b8" strokeWidth={1} />
          ))
        })}
        {/* Zero bank mark (top center) */}
        <polygon points={`${CX},${CY - R - 2} ${CX - 4},${CY - R - 8} ${CX + 4},${CY - R - 8}`}
          fill="#e2e8f0" />

        {/* Bank pointer (rotates with roll) */}
        <g transform={`rotate(${-rollDeg} ${CX} ${CY})`}>
          <polygon points={`${CX},${CY - R + 2} ${CX - 4},${CY - R + 8} ${CX + 4},${CY - R + 8}`}
            fill="#f59e0b" />
        </g>

        {/* Fixed aircraft symbol */}
        <line x1={CX - 28} y1={CY} x2={CX - 8} y2={CY} stroke="#f59e0b" strokeWidth={2.5} />
        <line x1={CX + 8} y1={CY} x2={CX + 28} y2={CY} stroke="#f59e0b" strokeWidth={2.5} />
        <circle cx={CX} cy={CY} r={3} fill="none" stroke="#f59e0b" strokeWidth={2} />
      </svg>
    </div>
  )
}
