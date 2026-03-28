export default function BarGauge({
  value = 0, max = 100, label = '', unit = '', decimals = 0,
  height = 100, width = 32, color = '#00ff88', warnBelow,
}) {
  const pct = max > 0 ? Math.max(0, Math.min(1, value / max)) : 0
  const warn = warnBelow != null && value < warnBelow
  const fillColor = warn ? '#f59e0b' : color

  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 2 }}>
      <div style={{
        width, height, background: '#0a0e14', border: '1px solid #1e293b', borderRadius: 3,
        position: 'relative', overflow: 'hidden',
      }}>
        <div style={{
          position: 'absolute', bottom: 0, left: 0, right: 0,
          height: `${pct * 100}%`, background: fillColor,
          transition: 'height 0.3s, background 0.3s', opacity: 0.8,
        }} />
      </div>
      <div style={{ color: '#e2e8f0', fontSize: 11, fontFamily: "'JetBrains Mono', monospace", fontWeight: 600 }}>
        {value.toFixed(decimals)}
      </div>
      {unit && <div style={{ color: '#64748b', fontSize: 8 }}>{unit}</div>}
      <div style={{ color: '#94a3b8', fontSize: 9, fontFamily: "'JetBrains Mono', monospace" }}>{label}</div>
    </div>
  )
}
