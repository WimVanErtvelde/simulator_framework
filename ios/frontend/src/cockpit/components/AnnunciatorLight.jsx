export default function AnnunciatorLight({ label, active = false, color = '#f59e0b' }) {
  return (
    <div style={{
      display: 'inline-flex', alignItems: 'center', justifyContent: 'center',
      width: 72, height: 24, borderRadius: 3,
      background: active ? color : '#1c2333',
      border: `1px solid ${active ? color : '#1e293b'}`,
      color: active ? '#000' : '#475569',
      fontSize: 9, fontWeight: 700, fontFamily: "'JetBrains Mono', monospace",
      letterSpacing: 0.5, transition: 'all 0.2s',
    }}>
      {label}
    </div>
  )
}
