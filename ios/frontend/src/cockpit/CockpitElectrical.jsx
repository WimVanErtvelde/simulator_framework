import { useSimStore } from '../store/useSimStore'

// Virtual cockpit electrical panel — publishes to /devices/virtual/panel
// This is the "pilot's panel" view, distinct from the IOS instructor view.
// Switches and buses are rendered dynamically from the electrical state.

function ToggleSwitch({ label, on, onToggle }) {
  return (
    <div style={{
      display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 6,
    }}>
      <button
        onClick={onToggle}
        style={{
          width: 40, height: 64, borderRadius: 4, cursor: 'pointer',
          border: '2px solid #475569',
          background: on
            ? 'linear-gradient(180deg, #334155 0%, #1e293b 100%)'
            : 'linear-gradient(180deg, #1e293b 0%, #334155 100%)',
          position: 'relative',
        }}
      >
        {/* Toggle lever */}
        <div style={{
          width: 24, height: 24, borderRadius: 3,
          background: on ? '#e2e8f0' : '#64748b',
          position: 'absolute', left: '50%', transform: 'translateX(-50%)',
          top: on ? 6 : 34,
          transition: 'top 0.15s, background 0.15s',
          boxShadow: '0 2px 4px rgba(0,0,0,0.4)',
        }} />
      </button>
      <span style={{
        fontSize: 10, fontWeight: 700, letterSpacing: 1, color: '#94a3b8',
        textAlign: 'center', maxWidth: 80,
      }}>{label}</span>
      <span style={{
        fontSize: 9, fontWeight: 700, color: on ? '#00ff88' : '#475569',
      }}>{on ? 'ON' : 'OFF'}</span>
    </div>
  )
}

export default function CockpitElectrical() {
  const { electrical, aircraftId } = useSimStore()

  // Virtual cockpit publishes to /devices/virtual/panel (via different WS message)
  const toggleVirtual = (id, currentState) => {
    const { ws, wsConnected } = useSimStore.getState()
    if (!wsConnected || !ws) return
    ws.send(JSON.stringify({
      type: 'set_virtual_panel',
      data: { switch_ids: [id], switch_states: [!currentState] },
    }))
  }

  return (
    <div style={{
      height: '100vh', width: '100vw',
      background: '#0a0e14',
      color: '#e2e8f0',
      fontFamily: "'JetBrains Mono', 'Fira Code', monospace",
      display: 'flex', flexDirection: 'column', alignItems: 'center',
      padding: 32,
    }}>
      {/* Panel label */}
      <div style={{
        fontSize: 11, fontWeight: 700, letterSpacing: 3, color: '#475569',
        textTransform: 'uppercase', marginBottom: 32,
      }}>
        {aircraftId.toUpperCase()} Electrical Panel — Virtual Cockpit
      </div>

      {/* Switch panel — rendered dynamically from solver state */}
      <div style={{
        background: '#111827', border: '1px solid #1e293b', borderRadius: 8,
        padding: '32px 48px', display: 'flex', gap: 32, alignItems: 'flex-start',
        flexWrap: 'wrap', justifyContent: 'center',
        boxShadow: '0 4px 24px rgba(0,0,0,0.5)',
      }}>
        {electrical.switchIds?.length > 0 ? (
          electrical.switchIds.map((id, i) => {
            const label = electrical.switchLabels?.[i] || id
            const on = electrical.switchClosed[i] ?? false
            return (
              <ToggleSwitch key={id} label={label} on={on}
                onToggle={() => toggleVirtual(id, on)} />
            )
          })
        ) : (
          <div style={{ color: '#64748b', fontSize: 13 }}>
            No switch data — waiting for electrical node
          </div>
        )}
      </div>

      {/* Bus voltage readout */}
      {electrical.busNames?.length > 0 && (
        <div style={{
          marginTop: 32, background: '#111827', border: '1px solid #1e293b',
          borderRadius: 8, padding: '16px 32px', minWidth: 300,
        }}>
          <div style={{
            fontSize: 10, fontWeight: 700, letterSpacing: 2, color: '#475569',
            marginBottom: 12, textTransform: 'uppercase',
          }}>Bus Voltages</div>
          {electrical.busNames.map((name, i) => {
            const v = electrical.busVoltagesV[i] ?? 0
            const color = v > 13 ? '#00ff88' : v > 11 ? '#f59e0b' : '#ef4444'
            return (
              <div key={name} style={{
                display: 'flex', justifyContent: 'space-between',
                padding: '4px 0', fontSize: 13,
              }}>
                <span style={{ color: '#94a3b8' }}>{name}</span>
                <span style={{ color, fontWeight: 700 }}>{v.toFixed(1)} V</span>
              </div>
            )
          })}
        </div>
      )}

      {/* Link back to IOS */}
      <a href="/" style={{
        marginTop: 32, fontSize: 11, color: '#475569', textDecoration: 'none',
      }}>← Back to IOS</a>
    </div>
  )
}
