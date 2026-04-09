import { useState } from 'react'
import { useSimStore } from '../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { PanelRow, SectionHeader } from '../PanelUtils'

function voltageColor(v) {
  if (v > 13) return '#00ff88'
  if (v > 11) return '#f59e0b'
  return '#ef4444'
}

function TriStateSelector({ label, value, onChange }) {
  const options = [
    { key: 0, text: 'AUTO', color: '#64748b' },
    { key: 1, text: 'ON',   color: '#00ff88' },
    { key: 2, text: 'OFF',  color: '#ef4444' },
  ]
  return (
    <div style={{
      display: 'flex', justifyContent: 'space-between', alignItems: 'center',
      padding: '4px 0', fontSize: 12, fontFamily: 'monospace',
    }}>
      <span style={{ color: '#94a3b8' }}>{label}</span>
      <div style={{ display: 'flex', gap: 2 }}>
        {options.map(o => (
          <button key={o.key} onClick={() => onChange(o.key)} style={{
            padding: '2px 8px', fontSize: 10, fontWeight: 700,
            fontFamily: 'monospace', border: 'none', borderRadius: 2,
            cursor: 'pointer', letterSpacing: 0.5,
            background: value === o.key ? o.color : '#1e293b',
            color: value === o.key ? '#0f172a' : '#475569',
          }}>{o.text}</button>
        ))}
      </div>
    </div>
  )
}

export default function ElectricalTab() {
  const { electrical, electricalConfig, forcedSwitchIds, sendPanel } = useSimStore(useShallow(s => ({
    electrical: s.electrical, electricalConfig: s.electricalConfig,
    forcedSwitchIds: s.forcedSwitchIds, sendPanel: s.sendPanel,
  })))

  const [groundSelectors, setGroundSelectors] = useState({})
  const [showLoads, setShowLoads] = useState(false)
  const [showCBs, setShowCBs] = useState(false)

  const cfg = electricalConfig

  // ── Ground services ──────────────────────────────────────────────
  const services = []
  electrical.sourceNames?.forEach((name) => {
    const upper = name.toUpperCase()
    if (upper.includes('EXT'))
      services.push({ selectorId: 'ext_pwr_cart', label: 'EXT PWR CART', sourceName: name })
    else if (upper.includes('APU'))
      services.push({ selectorId: 'apu_running', label: 'APU', sourceName: name })
  })

  // ── Electrical config-driven content ─────────────────────────────
  if (!cfg) {
    return (
      <div style={{ color: '#64748b', fontSize: 12, fontFamily: 'monospace' }}>
        Waiting for electrical config...
      </div>
    )
  }

  const swClosed = (id) => {
    const idx = electrical.switchIds?.indexOf(id)
    return idx >= 0 ? (electrical.switchClosed[idx] ?? false) : false
  }
  const isForced = (id) => forcedSwitchIds.includes(id)

  const sourceSwitches = (cfg.switches || [])
    .filter(sw => sw.pilot_controllable !== false)
  const loadSwitches = (cfg.loads || [])
    .filter(ld => ld.switch_id)
    .map(ld => ({ id: ld.switch_id, label: ld.label, type: 'load_switch' }))
  const allSwitches = [...sourceSwitches, ...loadSwitches]
    .filter((sw, i, arr) => arr.findIndex(s => s.id === sw.id) === i)
    .filter(sw => sw.id !== 'sw_starter_engage')

  const masterIds = new Set(sourceSwitches.map(s => s.id))
  const masterSwitches = allSwitches.filter(sw => masterIds.has(sw.id))
  const auxSwitches = allSwitches.filter(sw => !masterIds.has(sw.id))

  const renderSwitchRow = (sw) => {
    const on = swClosed(sw.id)
    const forced = isForced(sw.id)
    return (
      <div key={sw.id} style={{
        display: 'grid', gridTemplateColumns: '24px 1fr 48px',
        alignItems: 'center', gap: 8,
        padding: '4px 0', fontSize: 12, fontFamily: 'monospace',
      }}>
        <input
          type="checkbox"
          checked={forced}
          onClick={(e) => {
            e.stopPropagation()
            const newForced = !forced
            if (newForced) {
              sendPanel([sw.id], [on], null, null, [true])
            } else {
              sendPanel([sw.id], [], null, null, [false])
            }
          }}
          onChange={() => {}}
          title={forced ? 'Release force' : 'Force switch'}
          style={{ width: 14, height: 14, cursor: 'pointer', accentColor: '#f59e0b' }}
        />
        <span style={{ color: '#94a3b8', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
          {sw.label}
          {forced && (
            <span style={{
              fontSize: 8, fontWeight: 700, color: '#f59e0b', marginLeft: 6,
              letterSpacing: 1, verticalAlign: 'super',
            }}>FORCED</span>
          )}
        </span>
        <button
          onClick={() => sendPanel([sw.id], [!on], null, null, forced ? [true] : undefined)}
          style={{
            width: 48, height: 22, borderRadius: 11, cursor: 'pointer',
            border: '1px solid ' + (on ? '#f59e0b' : '#334155'),
            background: on ? '#f59e0b' : '#334155',
            position: 'relative', transition: 'background 0.2s, border-color 0.2s',
          }}
        >
          <div style={{
            width: 18, height: 18, borderRadius: '50%',
            background: '#1e293b', position: 'absolute', top: 1,
            left: on ? 27 : 1, transition: 'left 0.2s',
          }} />
        </button>
      </div>
    )
  }

  return (
    <div>
      {/* Ground services */}
      {services.length > 0 && (
        <>
          <SectionHeader title="GROUND SERVICES" />
          {services.map(svc => (
            <TriStateSelector
              key={svc.selectorId}
              label={svc.label}
              value={groundSelectors[svc.selectorId] ?? 0}
              onChange={(val) => {
                setGroundSelectors(prev => ({ ...prev, [svc.selectorId]: val }))
                sendPanel([], [], [svc.selectorId], [val])
              }}
            />
          ))}
          <div style={{ fontSize: 9, color: '#475569', fontFamily: 'monospace', marginTop: 2, marginBottom: 8 }}>
            AUTO = available on ground &nbsp; ON = force &nbsp; OFF = force disconnect
          </div>
        </>
      )}

      <SectionHeader title="ELECTRICAL" />

      {/* MASTER switches */}
      <div style={{ fontSize: 10, color: '#64748b', fontFamily: 'monospace', marginBottom: 4 }}>
        MASTER
      </div>
      {masterSwitches.map(renderSwitchRow)}

      {/* Auxiliary switches */}
      {auxSwitches.length > 0 && (
        <>
          <div style={{ fontSize: 10, color: '#64748b', fontFamily: 'monospace', marginTop: 8, marginBottom: 4 }}>
            SWITCHES
          </div>
          {auxSwitches.map(renderSwitchRow)}
        </>
      )}

      {/* Sources */}
      {electrical.sourceNames?.length > 0 && (
        <>
          <div style={{ fontSize: 10, color: '#64748b', fontFamily: 'monospace', marginTop: 8, marginBottom: 4 }}>
            SOURCES
          </div>
          {electrical.sourceNames.map((name, i) => (
            <div key={name} style={{
              display: 'flex', justifyContent: 'space-between', alignItems: 'center',
              padding: '2px 0', fontSize: 12, fontFamily: 'monospace',
            }}>
              <span style={{ color: '#94a3b8' }}>{name}</span>
              <span style={{ color: electrical.sourceActive[i] ? '#00ff88' : '#ef4444' }}>
                {electrical.sourceActive[i] ? 'ON' : 'OFF'}
                <span style={{ color: '#64748b', marginLeft: 8 }}>
                  {electrical.sourceVoltages[i]?.toFixed(1) ?? '--'}V
                </span>
                <span style={{ color: '#64748b', marginLeft: 6 }}>
                  {electrical.sourceCurrents[i]?.toFixed(1) ?? '--'}A
                </span>
              </span>
            </div>
          ))}
        </>
      )}

      {/* Buses */}
      {electrical.busNames?.length > 0 && (
        <>
          <div style={{ fontSize: 10, color: '#64748b', fontFamily: 'monospace', marginTop: 8, marginBottom: 4 }}>
            BUSES
          </div>
          {electrical.busNames.map((name, i) => (
            <PanelRow key={name} label={name}
              value={electrical.busVoltages[i]?.toFixed(1) ?? '--'}
              unit="V"
              valueStyle={{ color: voltageColor(electrical.busVoltages[i] ?? 0) }}
            />
          ))}
        </>
      )}

      {/* Summary */}
      <PanelRow label="Total Load" value={electrical.totalLoadAmps?.toFixed(1) ?? '--'} unit="A" />
      <PanelRow label="Battery SOC" value={electrical.batterySocPct?.toFixed(0) ?? '--'} unit="%" />
      <PanelRow label="Master Bus"
        value={electrical.masterBusVoltage?.toFixed(1) ?? '--'}
        unit="V"
        valueStyle={{ color: voltageColor(electrical.masterBusVoltage ?? 0) }}
      />

      {/* Loads (collapsible) */}
      {electrical.loadNames?.length > 0 && (
        <>
          <div
            onClick={() => setShowLoads(p => !p)}
            style={{
              fontSize: 10, color: '#64748b', fontFamily: 'monospace',
              marginTop: 8, marginBottom: 4, cursor: 'pointer', userSelect: 'none',
            }}
          >
            {showLoads ? '▾' : '▸'} LOADS ({electrical.loadNames.length})
          </div>
          {showLoads && electrical.loadNames.map((name, i) => (
            <div key={name} style={{
              display: 'flex', justifyContent: 'space-between', alignItems: 'center',
              padding: '1px 0 1px 12px', fontSize: 11, fontFamily: 'monospace',
            }}>
              <span style={{ color: electrical.loadPowered[i] ? '#94a3b8' : '#475569' }}>{name}</span>
              <span style={{ color: electrical.loadPowered[i] ? '#00ff88' : '#475569' }}>
                {electrical.loadPowered[i] ? `${electrical.loadCurrents[i]?.toFixed(1) ?? '0'}A` : 'OFF'}
              </span>
            </div>
          ))}
        </>
      )}

      {/* Circuit Breakers (interactive) */}
      {electrical.cbNames?.length > 0 && (
        <>
          <div
            onClick={() => setShowCBs(p => !p)}
            style={{
              fontSize: 10, color: '#64748b', fontFamily: 'monospace',
              marginTop: 8, marginBottom: 4, cursor: 'pointer', userSelect: 'none',
            }}
          >
            {showCBs ? '▾' : '▸'} CIRCUIT BREAKERS ({electrical.cbNames.length})
          </div>
          {showCBs && electrical.cbNames.map((name, i) => {
            const closed = electrical.cbClosed[i]
            const tripped = electrical.cbTripped[i]
            const forced = isForced(name)
            const stateLabel = tripped ? 'TRIPPED' : closed ? 'IN' : 'POPPED'
            const stateColor = forced ? '#ff3b30' : tripped ? '#f59e0b' : closed ? '#00ff88' : '#f59e0b'
            return (
              <div key={name} style={{
                display: 'grid', gridTemplateColumns: '24px 1fr 56px',
                alignItems: 'center', gap: 8,
                padding: '4px 0', fontSize: 12, fontFamily: 'monospace',
              }}>
                <input
                  type="checkbox"
                  checked={forced}
                  onClick={(e) => {
                    e.stopPropagation()
                    const newForced = !forced
                    if (newForced) {
                      sendPanel([name], [closed], null, null, [true])
                    } else {
                      sendPanel([name], [], null, null, [false])
                    }
                  }}
                  onChange={() => {}}
                  title={forced ? 'Release force' : 'Force CB'}
                  style={{ width: 14, height: 14, cursor: 'pointer', accentColor: '#f59e0b' }}
                />
                <span style={{ color: '#94a3b8', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                  {name.replace('cb_', '').toUpperCase()}
                  {forced && (
                    <span style={{
                      fontSize: 8, fontWeight: 700, color: '#f59e0b', marginLeft: 6,
                      letterSpacing: 1, verticalAlign: 'super',
                    }}>LOCKED</span>
                  )}
                </span>
                <button
                  onClick={() => {
                    sendPanel([name], [!closed], null, null, forced ? [true] : undefined)
                  }}
                  style={{
                    width: 56, height: 22, borderRadius: 4, cursor: 'pointer',
                    border: '1px solid ' + (forced ? '#ff3b30' : stateColor),
                    background: closed ? '#1c2333' : (forced ? '#3b1111' : '#332200'),
                    color: stateColor, fontSize: 10, fontWeight: 600,
                    fontFamily: 'monospace',
                  }}
                >
                  {stateLabel}
                </button>
              </div>
            )
          })}
        </>
      )}
    </div>
  )
}
