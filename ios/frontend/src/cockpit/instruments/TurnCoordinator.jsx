const SIZE = 180, CX = 90, CY = 90, R = 72

export default function TurnCoordinator({ yawRateRads = 0, rollDeg = 0 }) {
  // Standard rate turn = 3 deg/s = ~0.0524 rad/s → tilt airplane 20 degrees
  const tilt = Math.max(-30, Math.min(30, (yawRateRads / 0.0524) * 20))
  // Slip ball: simplified — driven by (roll - coordinated roll for yaw rate)
  const ballOffset = Math.max(-20, Math.min(20, rollDeg * 0.3))

  return (
    <div style={{ textAlign: 'center' }}>
      <svg width={SIZE} height={SIZE} viewBox={`0 0 ${SIZE} ${SIZE}`}>
        <circle cx={CX} cy={CY} r={R + 6} fill="#111827" stroke="#1e293b" strokeWidth={1.5} />

        {/* Rate marks (L standard, R standard) */}
        {[-20, 20].map(deg => {
          const a = ((-90 + deg) * Math.PI) / 180
          return <line key={deg}
            x1={CX + (R - 10) * Math.cos(a)} y1={CY + (R - 10) * Math.sin(a)}
            x2={CX + R * Math.cos(a)} y2={CY + R * Math.sin(a)}
            stroke="#e2e8f0" strokeWidth={2} />
        })}

        {/* Tilting airplane symbol */}
        <g transform={`rotate(${-tilt} ${CX} ${CY})`}>
          <line x1={CX - 36} y1={CY} x2={CX - 8} y2={CY} stroke="#e2e8f0" strokeWidth={2.5} />
          <line x1={CX + 8} y1={CY} x2={CX + 36} y2={CY} stroke="#e2e8f0" strokeWidth={2.5} />
          <circle cx={CX} cy={CY} r={4} fill="none" stroke="#e2e8f0" strokeWidth={2} />
          <line x1={CX} y1={CY + 4} x2={CX} y2={CY + 14} stroke="#e2e8f0" strokeWidth={2} />
        </g>

        {/* Slip/skid ball tube */}
        <rect x={CX - 28} y={CY + 36} width={56} height={12} rx={6}
          fill="none" stroke="#64748b" strokeWidth={1} />
        {/* Center marks */}
        <line x1={CX - 5} y1={CY + 36} x2={CX - 5} y2={CY + 48} stroke="#64748b" strokeWidth={0.7} />
        <line x1={CX + 5} y1={CY + 36} x2={CX + 5} y2={CY + 48} stroke="#64748b" strokeWidth={0.7} />
        {/* Ball */}
        <circle cx={CX + ballOffset} cy={CY + 42} r={5} fill="#1a1a2e" stroke="#e2e8f0" strokeWidth={1} />

        <text x={CX} y={CY - 30} fill="#64748b" fontSize={8} textAnchor="middle"
          fontFamily="'JetBrains Mono', monospace">TURN COORDINATOR</text>
        <text x={CX - 40} y={CY + 56} fill="#64748b" fontSize={7}
          fontFamily="'JetBrains Mono', monospace">L</text>
        <text x={CX + 37} y={CY + 56} fill="#64748b" fontSize={7}
          fontFamily="'JetBrains Mono', monospace">R</text>
      </svg>
    </div>
  )
}
