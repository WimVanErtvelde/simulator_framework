import ElectricalSection from './sections/ElectricalSection'

// Virtual cockpit electrical panel — standalone full-screen wrapper.
// All logic lives in ElectricalSection; this is just chrome.

export default function CockpitElectrical() {
  return (
    <div style={{
      height: '100vh', width: '100vw', background: '#0a0e14',
      color: '#e2e8f0', padding: 32, overflow: 'auto',
      fontFamily: "'JetBrains Mono', 'Fira Code', monospace",
      display: 'flex', flexDirection: 'column', alignItems: 'center',
    }}>
      <div style={{
        fontSize: 11, fontWeight: 700, letterSpacing: 3,
        color: '#475569', textTransform: 'uppercase', marginBottom: 32,
      }}>
        Electrical Panel — Virtual Cockpit
      </div>
      <ElectricalSection />
      <a href="/" style={{
        marginTop: 32, fontSize: 11, color: '#475569', textDecoration: 'none',
      }}>← Back to IOS</a>
    </div>
  )
}
