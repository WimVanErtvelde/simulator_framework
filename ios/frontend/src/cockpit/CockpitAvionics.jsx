// Virtual cockpit avionics panel — publishes to /aircraft/devices/virtual/panel
// TODO: implement full C172 avionics stack (COM/NAV/XPDR/ADF)

export default function CockpitAvionics() {
  return (
    <div style={{
      height: '100vh', width: '100vw',
      background: '#0a0e14',
      color: '#e2e8f0',
      fontFamily: "'JetBrains Mono', 'Fira Code', monospace",
      display: 'flex', flexDirection: 'column', alignItems: 'center',
      justifyContent: 'center',
    }}>
      <div style={{
        fontSize: 11, fontWeight: 700, letterSpacing: 3, color: '#475569',
        textTransform: 'uppercase', marginBottom: 16,
      }}>
        C172 Avionics Panel — Virtual Cockpit
      </div>
      <div style={{
        background: '#111827', border: '1px solid #1e293b', borderRadius: 8,
        padding: '48px 64px',
        boxShadow: '0 4px 24px rgba(0,0,0,0.5)',
      }}>
        <div style={{ color: '#64748b', fontSize: 14 }}>
          COM / NAV / XPDR / ADF — coming soon
        </div>
      </div>
      <a href="/" style={{
        marginTop: 32, fontSize: 11, color: '#475569', textDecoration: 'none',
      }}>← Back to IOS</a>
    </div>
  )
}
