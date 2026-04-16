import RadioSection from './sections/RadioSection'

// Virtual cockpit avionics panel — standalone full-screen wrapper.
// All logic lives in RadioSection; this is just chrome.

export default function CockpitAvionics() {
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
        Avionics Panel — Virtual Cockpit
      </div>
      <div style={{
        background: '#111827', border: '1px solid #1e293b', borderRadius: 8,
        padding: '24px 32px', minWidth: 320, maxWidth: 480, width: '100%',
        boxShadow: '0 4px 24px rgba(0,0,0,0.5)',
      }}>
        <RadioSection />
      </div>
      <a href="/" style={{
        marginTop: 32, fontSize: 11, color: '#475569', textDecoration: 'none',
      }}>← Back to IOS</a>
    </div>
  )
}
