const SIZE = 120
const CX = SIZE / 2, CY = SIZE / 2, R = 50

export default function ControlPositionIndicator({ aileron = 0, elevator = 0, rudder = 0 }) {
  // aileron: -1(left) to +1(right), elevator: -1(nose down) to +1(nose up), rudder: -1(left) to +1(right)
  const dotX = CX + aileron * R
  const dotY = CY - elevator * R  // invert: nose up = up on screen
  const rudderX = CX + rudder * R

  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 4 }}>
      <svg width={SIZE} height={SIZE} viewBox={`0 0 ${SIZE} ${SIZE}`}>
        {/* Background */}
        <rect x={CX - R} y={CY - R} width={R * 2} height={R * 2}
          fill="#0a0e14" stroke="#1e293b" strokeWidth={1} rx={4} />
        {/* Center crosshair */}
        <line x1={CX - R} y1={CY} x2={CX + R} y2={CY} stroke="#1e293b" strokeWidth={0.5} />
        <line x1={CX} y1={CY - R} x2={CX} y2={CY + R} stroke="#1e293b" strokeWidth={0.5} />
        {/* Dot */}
        <circle cx={dotX} cy={dotY} r={6} fill="#00ff88" opacity={0.9} />
        <circle cx={dotX} cy={dotY} r={3} fill="#fff" />
        {/* Labels */}
        <text x={CX - R - 4} y={CY} fill="#475569" fontSize={7} textAnchor="end"
          dominantBaseline="central" fontFamily="monospace">L</text>
        <text x={CX + R + 4} y={CY} fill="#475569" fontSize={7} textAnchor="start"
          dominantBaseline="central" fontFamily="monospace">R</text>
        <text x={CX} y={CY - R - 4} fill="#475569" fontSize={7} textAnchor="middle"
          fontFamily="monospace">U</text>
        <text x={CX} y={CY + R + 8} fill="#475569" fontSize={7} textAnchor="middle"
          fontFamily="monospace">D</text>
      </svg>
      {/* Rudder bar below */}
      <svg width={SIZE} height={20} viewBox={`0 0 ${SIZE} 20`}>
        <rect x={CX - R} y={4} width={R * 2} height={12}
          fill="#0a0e14" stroke="#1e293b" strokeWidth={1} rx={3} />
        <line x1={CX} y1={4} x2={CX} y2={16} stroke="#1e293b" strokeWidth={0.5} />
        <rect x={rudderX - 6} y={5} width={12} height={10}
          fill="#00ff88" rx={2} opacity={0.9} />
      </svg>
      <div style={{ color: '#475569', fontSize: 8, fontFamily: "'JetBrains Mono', monospace" }}>
        A {aileron.toFixed(2)} E {elevator.toFixed(2)} R {rudder.toFixed(2)}
      </div>
    </div>
  )
}
