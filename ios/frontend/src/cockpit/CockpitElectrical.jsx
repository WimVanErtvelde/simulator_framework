import { useSimStore } from '../store/useSimStore'

// Virtual cockpit electrical panel — publishes to /devices/virtual/panel
// Switches loaded dynamically from electricalConfig (electrical.yaml).
// NO FORCE checkboxes — that's IOS-only. Green styling = VIRTUAL priority.

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

function voltageColor(v) {
  if (v > 13) return '#00ff88'
  if (v > 11) return '#f59e0b'
  return '#ef4444'
}

export default function CockpitElectrical() {
  const { electrical, electricalConfig, aircraftId } = useSimStore()

  const toggleVirtual = (id, currentState) => {
    const { ws, wsConnected } = useSimStore.getState()
    if (!wsConnected || !ws) return
    ws.send(JSON.stringify({
      type: 'set_virtual_panel',
      data: { switch_ids: [id], switch_states: [!currentState] },
    }))
  }

  // Look up live switch state by ID from ElectricalState
  const swClosed = (id) => {
    const idx = electrical.switchIds?.indexOf(id)
    return idx >= 0 ? (electrical.switchClosed[idx] ?? false) : false
  }

  const cfg = electricalConfig

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

      {/* Switch panel — from electricalConfig */}
      <div style={{
        background: '#111827', border: '1px solid #1e293b', borderRadius: 8,
        padding: '32px 48px', display: 'flex', gap: 32, alignItems: 'flex-start',
        flexWrap: 'wrap', justifyContent: 'center',
        boxShadow: '0 4px 24px rgba(0,0,0,0.5)',
      }}>
        {cfg ? (
          cfg.switches?.filter(s => s.pilot_controllable !== false).map(sw => {
            const on = swClosed(sw.id)
            return (
              <ToggleSwitch key={sw.id} label={sw.label} on={on}
                onToggle={() => toggleVirtual(sw.id, on)} />
            )
          })
        ) : (
          <div style={{ color: '#64748b', fontSize: 13 }}>
            Waiting for electrical config...
          </div>
        )}
      </div>

      {/* Sources */}
      {electrical.sourceNames?.length > 0 && (
        <div style={{
          marginTop: 32, background: '#111827', border: '1px solid #1e293b',
          borderRadius: 8, padding: '16px 32px', minWidth: 300,
        }}>
          <div style={{
            fontSize: 10, fontWeight: 700, letterSpacing: 2, color: '#475569',
            marginBottom: 12, textTransform: 'uppercase',
          }}>Sources</div>
          {electrical.sourceNames.map((name, i) => (
            <div key={name} style={{
              display: 'flex', justifyContent: 'space-between',
              padding: '4px 0', fontSize: 13,
            }}>
              <span style={{ color: '#94a3b8' }}>{name}</span>
              <span>
                <span style={{ color: electrical.sourceActive[i] ? '#00ff88' : '#ef4444', fontWeight: 700 }}>
                  {electrical.sourceActive[i] ? 'ON' : 'OFF'}
                </span>
                <span style={{ color: '#64748b', marginLeft: 10 }}>
                  {electrical.sourceVoltages[i]?.toFixed(1) ?? '--'}V
                </span>
                <span style={{ color: '#64748b', marginLeft: 8 }}>
                  {electrical.sourceCurrents[i]?.toFixed(1) ?? '--'}A
                </span>
              </span>
            </div>
          ))}
        </div>
      )}

      {/* Bus voltages */}
      {electrical.busNames?.length > 0 && (
        <div style={{
          marginTop: 16, background: '#111827', border: '1px solid #1e293b',
          borderRadius: 8, padding: '16px 32px', minWidth: 300,
        }}>
          <div style={{
            fontSize: 10, fontWeight: 700, letterSpacing: 2, color: '#475569',
            marginBottom: 12, textTransform: 'uppercase',
          }}>Bus Voltages</div>
          {electrical.busNames.map((name, i) => {
            const v = electrical.busVoltages[i] ?? 0
            return (
              <div key={name} style={{
                display: 'flex', justifyContent: 'space-between',
                padding: '4px 0', fontSize: 13,
              }}>
                <span style={{ color: '#94a3b8' }}>{name}</span>
                <span style={{ color: voltageColor(v), fontWeight: 700 }}>{v.toFixed(1)} V</span>
              </div>
            )
          })}
        </div>
      )}

      {/* Summary */}
      <div style={{
        marginTop: 16, background: '#111827', border: '1px solid #1e293b',
        borderRadius: 8, padding: '16px 32px', minWidth: 300,
      }}>
        <div style={{
          display: 'flex', justifyContent: 'space-between',
          padding: '4px 0', fontSize: 13,
        }}>
          <span style={{ color: '#94a3b8' }}>Total Load</span>
          <span style={{ color: '#e2e8f0', fontWeight: 700 }}>
            {electrical.totalLoadAmps?.toFixed(1) ?? '--'} A
          </span>
        </div>
        <div style={{
          display: 'flex', justifyContent: 'space-between',
          padding: '4px 0', fontSize: 13,
        }}>
          <span style={{ color: '#94a3b8' }}>Battery SOC</span>
          <span style={{ color: '#e2e8f0', fontWeight: 700 }}>
            {electrical.batterySocPct?.toFixed(0) ?? '--'}%
          </span>
        </div>
        <div style={{
          display: 'flex', justifyContent: 'space-between',
          padding: '4px 0', fontSize: 13,
        }}>
          <span style={{ color: '#94a3b8' }}>Master Bus</span>
          <span style={{ color: voltageColor(electrical.masterBusVoltage ?? 0), fontWeight: 700 }}>
            {electrical.masterBusVoltage?.toFixed(1) ?? '--'} V
          </span>
        </div>
      </div>

      {/* Link back to IOS */}
      <a href="/" style={{
        marginTop: 32, fontSize: 11, color: '#475569', textDecoration: 'none',
      }}>← Back to IOS</a>
    </div>
  )
}
