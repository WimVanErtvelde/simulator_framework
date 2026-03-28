export default function SelectorControl({ label, options, value, onChange }) {
  // options: [{value: 0, label: 'OFF'}, {value: 1, label: 'R'}, ...]
  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 4 }}>
      <div style={{ color: '#94a3b8', fontSize: 9, fontFamily: "'JetBrains Mono', monospace" }}>{label}</div>
      <div style={{ display: 'flex', gap: 2 }}>
        {options.map(opt => {
          const active = opt.value === value
          return (
            <button key={opt.value} onClick={() => onChange(opt.value)} style={{
              padding: '4px 8px', fontSize: 10, fontWeight: 600,
              fontFamily: "'JetBrains Mono', monospace",
              border: `1px solid ${active ? '#00ff88' : '#1e293b'}`,
              borderRadius: 3, cursor: 'pointer',
              background: active ? 'rgba(0,255,136,0.15)' : '#111827',
              color: active ? '#00ff88' : '#64748b',
              minWidth: 32, transition: 'all 0.15s',
            }}>
              {opt.label}
            </button>
          )
        })}
      </div>
    </div>
  )
}
