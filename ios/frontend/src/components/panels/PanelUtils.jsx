export function PanelRow({ label, value, unit, highlight, valueStyle }) {
  return (
    <div style={{
      display: 'flex', justifyContent: 'space-between', alignItems: 'center',
      padding: '8px 0', borderBottom: '1px solid #1e293b', minHeight: 40,
    }}>
      <span style={{ color: '#64748b', fontSize: 14, fontFamily: "'JetBrains Mono', 'Fira Code', monospace" }}>{label}</span>
      <span style={{
        color: highlight ? '#00ff88' : '#e2e8f0',
        fontSize: 15, fontWeight: 700, fontFamily: "'JetBrains Mono', 'Fira Code', monospace",
        ...valueStyle,
      }}>{value}{unit ? ` ${unit}` : ''}</span>
    </div>
  )
}

export function SectionHeader({ title }) {
  return (
    <div style={{
      fontSize: 11, fontWeight: 700, letterSpacing: 2,
      color: '#39d0d8', textTransform: 'uppercase',
      padding: '12px 0 6px', borderBottom: '1px solid #1e293b', marginBottom: 10,
    }}>{title}</div>
  )
}

export function FullWidthBtn({ label, style, onClick, disabled }) {
  return (
    <button
      onClick={(e) => { e.preventDefault(); onClick?.() }}
      onTouchEnd={(e) => { e.preventDefault(); onClick?.() }}
      disabled={disabled}
      style={{
        width: '100%', height: 48, borderRadius: 3, fontFamily: 'monospace',
        fontSize: 13, fontWeight: 700, letterSpacing: 1, textTransform: 'uppercase',
        cursor: disabled ? 'not-allowed' : 'pointer',
        border: '1px solid #1e293b', touchAction: 'manipulation',
        transition: 'opacity 0.15s',
        opacity: disabled ? 0.4 : 1,
        background: 'rgba(0, 255, 136, 0.08)',
        color: '#00ff88',
        borderColor: '#00ff88',
        ...style,
      }}
    >{label}</button>
  )
}
