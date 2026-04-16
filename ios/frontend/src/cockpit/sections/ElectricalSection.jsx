import { useSimStore } from '../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'

// Embeddable electrical switch/source/bus section.
// Used by CockpitElectrical (standalone) and C172Panel (compact embed).
// Reads from store directly — no props required except optional `compact`.

function ToggleSwitch({ label, on, onToggle, size = 'normal' }) {
  const compact = size === 'compact'
  const w = compact ? 32 : 40
  const h = compact ? 50 : 64
  const knob = compact ? 18 : 24
  return (
    <div style={{
      display: 'flex', flexDirection: 'column', alignItems: 'center',
      gap: compact ? 3 : 6,
    }}>
      <button
        onClick={onToggle}
        style={{
          width: w, height: h, borderRadius: 4, cursor: 'pointer',
          border: '2px solid #475569',
          background: on
            ? 'linear-gradient(180deg, #334155 0%, #1e293b 100%)'
            : 'linear-gradient(180deg, #1e293b 0%, #334155 100%)',
          position: 'relative',
        }}
      >
        <div style={{
          width: knob, height: knob, borderRadius: 3,
          background: on ? '#e2e8f0' : '#64748b',
          position: 'absolute', left: '50%', transform: 'translateX(-50%)',
          top: on ? 6 : (h - knob - 6),
          transition: 'top 0.15s, background 0.15s',
          boxShadow: '0 2px 4px rgba(0,0,0,0.4)',
        }} />
      </button>
      <span style={{
        fontSize: compact ? 8 : 10, fontWeight: 700, letterSpacing: 1,
        color: '#94a3b8', textAlign: 'center', maxWidth: 80,
      }}>{label}</span>
      <span style={{
        fontSize: compact ? 8 : 9, fontWeight: 700,
        color: on ? '#00ff88' : '#475569',
      }}>{on ? 'ON' : 'OFF'}</span>
    </div>
  )
}

function voltageColor(v) {
  if (v > 13) return '#00ff88'
  if (v > 11) return '#f59e0b'
  return '#ef4444'
}

export default function ElectricalSection({ compact = false }) {
  const { electrical, electricalConfig, forcedSwitchIds } = useSimStore(useShallow(s => ({
    electrical: s.electrical, electricalConfig: s.electricalConfig,
    forcedSwitchIds: s.forcedSwitchIds ?? [],
  })))

  const toggleVirtual = (id, currentState) => {
    if (forcedSwitchIds.includes(id)) return
    const { ws, wsConnected } = useSimStore.getState()
    if (!wsConnected || !ws) return
    ws.send(JSON.stringify({
      type: 'set_virtual_panel',
      data: { switch_ids: [id], switch_states: [!currentState] },
    }))
  }

  const swClosed = (id) => {
    const idx = electrical.switchIds?.indexOf(id)
    return idx >= 0 ? (electrical.switchClosed[idx] ?? false) : false
  }

  const cfg = electricalConfig

  const sourceSwitches = (cfg?.switches || [])
    .filter(sw => sw.pilot_controllable !== false)
  const loadSwitches = (cfg?.loads || [])
    .filter(ld => ld.switch_id)
    .map(ld => ({ id: ld.switch_id, label: ld.label }))
  const allSwitches = [...sourceSwitches, ...loadSwitches]
    .filter((sw, i, arr) => arr.findIndex(s => s.id === sw.id) === i)
    .filter(sw => sw.id !== 'sw_starter_engage')

  const masterIds = new Set(sourceSwitches.map(s => s.id))
  const masterGroup = allSwitches.filter(sw => masterIds.has(sw.id))
  const auxGroup = allSwitches.filter(sw => !masterIds.has(sw.id))

  const pad = compact ? '12px 16px' : '32px 48px'
  const gap = compact ? 16 : 32
  const switchSize = compact ? 'compact' : 'normal'

  if (!cfg) {
    return (
      <div style={{
        background: '#111827', border: '1px solid #1e293b', borderRadius: 8,
        padding: pad, color: '#64748b', fontSize: 13,
      }}>
        Waiting for electrical config...
      </div>
    )
  }

  return (
    <div>
      {/* MASTER switches */}
      <div style={{
        background: compact ? 'transparent' : '#111827',
        border: compact ? 'none' : '1px solid #1e293b',
        borderRadius: 8, padding: pad,
        boxShadow: compact ? 'none' : '0 4px 24px rgba(0,0,0,0.5)',
      }}>
        <div style={{
          fontSize: 10, fontWeight: 700, letterSpacing: 2, color: '#475569',
          textTransform: 'uppercase', marginBottom: compact ? 8 : 16,
          textAlign: 'center',
        }}>Master</div>
        <div style={{ display: 'flex', gap, flexWrap: 'wrap', justifyContent: 'center' }}>
          {masterGroup.map(sw => {
            const on = swClosed(sw.id)
            return (
              <ToggleSwitch key={sw.id} label={sw.label} on={on} size={switchSize}
                onToggle={() => toggleVirtual(sw.id, on)} />
            )
          })}
        </div>
      </div>

      {/* AUX switches */}
      {auxGroup.length > 0 && (
        <div style={{
          marginTop: compact ? 8 : 16,
          background: compact ? 'transparent' : '#111827',
          border: compact ? 'none' : '1px solid #1e293b',
          borderRadius: 8, padding: pad,
          boxShadow: compact ? 'none' : '0 4px 24px rgba(0,0,0,0.5)',
        }}>
          <div style={{
            fontSize: 10, fontWeight: 700, letterSpacing: 2, color: '#475569',
            textTransform: 'uppercase', marginBottom: compact ? 8 : 16,
            textAlign: 'center',
          }}>Switches</div>
          <div style={{ display: 'flex', gap, flexWrap: 'wrap', justifyContent: 'center' }}>
            {auxGroup.map(sw => {
              const on = swClosed(sw.id)
              return (
                <ToggleSwitch key={sw.id} label={sw.label} on={on} size={switchSize}
                  onToggle={() => toggleVirtual(sw.id, on)} />
              )
            })}
          </div>
        </div>
      )}

      {/* Sources — only in standalone mode */}
      {!compact && electrical.sourceNames?.length > 0 && (
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

      {/* Bus voltages — only in standalone mode */}
      {!compact && electrical.busNames?.length > 0 && (
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

      {/* Summary — only in standalone mode */}
      {!compact && (
        <div style={{
          marginTop: 16, background: '#111827', border: '1px solid #1e293b',
          borderRadius: 8, padding: '16px 32px', minWidth: 300,
        }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', padding: '4px 0', fontSize: 13 }}>
            <span style={{ color: '#94a3b8' }}>Total Load</span>
            <span style={{ color: '#e2e8f0', fontWeight: 700 }}>{electrical.totalLoadAmps?.toFixed(1) ?? '--'} A</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between', padding: '4px 0', fontSize: 13 }}>
            <span style={{ color: '#94a3b8' }}>Battery SOC</span>
            <span style={{ color: '#e2e8f0', fontWeight: 700 }}>{electrical.batterySocPct?.toFixed(0) ?? '--'}%</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between', padding: '4px 0', fontSize: 13 }}>
            <span style={{ color: '#94a3b8' }}>Master Bus</span>
            <span style={{ color: voltageColor(electrical.masterBusVoltage ?? 0), fontWeight: 700 }}>
              {electrical.masterBusVoltage?.toFixed(1) ?? '--'} V
            </span>
          </div>
        </div>
      )}
    </div>
  )
}
