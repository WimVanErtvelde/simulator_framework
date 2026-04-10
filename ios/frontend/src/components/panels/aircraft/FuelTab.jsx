import { useState, useEffect, useCallback } from 'react'
import { useSimStore } from '../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { PanelRow, SectionHeader } from '../PanelUtils'

const LBS_TO_KG = 0.453592

function WbSlider({ label, value, max, unit, onChange }) {
  return (
    <div style={{ marginBottom: 6 }}>
      <div style={{
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
        fontSize: 12, fontFamily: 'monospace', marginBottom: 2,
      }}>
        <span style={{ color: '#94a3b8' }}>{label}</span>
        <span style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
          <input
            type="number"
            value={value.toFixed(1)}
            onChange={e => {
              const v = parseFloat(e.target.value)
              if (!isNaN(v)) onChange(Math.max(0, Math.min(max, v)))
            }}
            style={{
              width: 60, padding: '1px 4px', fontSize: 12, fontFamily: 'monospace',
              background: '#111827', color: '#e2e8f0', border: '1px solid #1e293b',
              borderRadius: 2, textAlign: 'right',
            }}
          />
          <span style={{ color: '#64748b', fontSize: 10, width: 24 }}>{unit}</span>
        </span>
      </div>
      <input
        type="range" min={0} max={max} step={0.1} value={value}
        onChange={e => onChange(parseFloat(e.target.value))}
        style={{ width: '100%', accentColor: '#39d0d8' }}
      />
    </div>
  )
}

function WeightDonut({ current, max, unit }) {
  const pct = max > 0 ? Math.min(current / max, 1) : 0
  const over = current > max
  const r = 44
  const circ = 2 * Math.PI * r
  const stroke = circ * pct
  return (
    <div style={{ textAlign: 'center' }}>
      <svg width={100} height={100} viewBox="0 0 100 100">
        <circle cx={50} cy={50} r={r} fill="none" stroke="#1e293b" strokeWidth={8} />
        <circle cx={50} cy={50} r={r} fill="none"
          stroke={over ? '#ff3b30' : '#39d0d8'} strokeWidth={8}
          strokeDasharray={`${stroke} ${circ}`}
          strokeLinecap="round"
          transform="rotate(-90 50 50)"
        />
        <text x={50} y={46} textAnchor="middle" fill="#e2e8f0"
          fontSize={14} fontFamily="monospace" fontWeight={700}>
          {current.toFixed(0)}
        </text>
        <text x={50} y={60} textAnchor="middle" fill="#64748b"
          fontSize={9} fontFamily="monospace">
          {unit}
        </text>
      </svg>
      <div style={{ fontSize: 9, color: '#475569', fontFamily: 'monospace' }}>
        MAX {max.toFixed(0)} {unit}
      </div>
    </div>
  )
}

export default function FuelTab() {
  const { fuel, fuelConfig, aircraftId, fdm, weightConfig, sendPayload, sendFuelLoading, simState } = useSimStore(useShallow(s => ({
    fuel: s.fuel, fuelConfig: s.fuelConfig, aircraftId: s.aircraftId,
    fdm: s.fdm, weightConfig: s.weightConfig, sendPayload: s.sendPayload,
    sendFuelLoading: s.sendFuelLoading, simState: s.simState,
  })))

  const [unitLbs, setUnitLbs] = useState(true)
  const [payloadWeights, setPayloadWeights] = useState({})
  const [fuelWeights, setFuelWeights] = useState({})
  const [initialized, setInitialized] = useState(false)

  // Initialize from weightConfig defaults on first load
  useEffect(() => {
    if (weightConfig && !initialized) {
      const pw = {}
      for (const s of weightConfig.payload_stations || []) {
        pw[s.index] = s.default_lbs
      }
      setPayloadWeights(pw)
      const fw = {}
      for (const t of weightConfig.fuel_stations || []) {
        fw[t.tank_index] = t.default_lbs
      }
      setFuelWeights(fw)
      setInitialized(true)
    }
  }, [weightConfig, initialized])

  // Sync fuel weights from solver when sim is RUNNING (fuel being consumed)
  useEffect(() => {
    if (simState !== 'RUNNING' || !weightConfig) return
    const fw = {}
    const KG_TO_LBS = 1 / LBS_TO_KG
    fuel.tanks.forEach((tank, i) => {
      const fuelStation = (weightConfig.fuel_stations || [])[i]
      if (fuelStation) {
        fw[fuelStation.tank_index] = (tank.massKg ?? 0) * KG_TO_LBS
      }
    })
    if (Object.keys(fw).length > 0) setFuelWeights(fw)
  }, [simState, fuel.tanks, weightConfig])

  const updateStation = useCallback((index, lbs) => {
    setPayloadWeights(prev => ({ ...prev, [index]: lbs }))
    sendPayload([{ index, weight_lbs: lbs }])
  }, [sendPayload])

  const updateFuelTank = useCallback((tankIndex, lbs) => {
    setFuelWeights(prev => {
      const next = { ...prev, [tankIndex]: lbs }
      const tanks = Object.entries(next).map(([idx, qty]) => ({
        index: parseInt(idx), quantity_lbs: qty
      }))
      sendFuelLoading(tanks)
      return next
    })
  }, [sendFuelLoading])

  const totalFuelCapacity = (weightConfig?.fuel_stations || []).reduce((s, t) => s + t.capacity_lbs, 0)
  const totalFuel = Object.values(fuelWeights).reduce((s, v) => s + v, 0)

  const setTotalFuel = useCallback((targetLbs) => {
    const stations = weightConfig?.fuel_stations || []
    const updates = {}
    const wsTanks = []
    for (const t of stations) {
      const proportion = totalFuelCapacity > 0 ? t.capacity_lbs / totalFuelCapacity : 1 / stations.length
      const v = Math.min(targetLbs * proportion, t.capacity_lbs)
      updates[t.tank_index] = v
      wsTanks.push({ index: t.tank_index, quantity_lbs: v })
    }
    setFuelWeights(prev => ({ ...prev, ...updates }))
    sendFuelLoading(wsTanks)
  }, [weightConfig, totalFuelCapacity, sendFuelLoading])

  const totalPayloadMax = (weightConfig?.payload_stations || []).reduce((s, p) => s + p.max_lbs, 0)
  const totalPayload = Object.values(payloadWeights).reduce((s, v) => s + v, 0)

  const setTotalPayload = useCallback((targetLbs) => {
    const stations = weightConfig?.payload_stations || []
    const currentTotal = Object.values(payloadWeights).reduce((s, v) => s + v, 0)
    if (currentTotal <= 0 && targetLbs > 0) {
      // Distribute evenly
      const perStation = targetLbs / stations.length
      const updates = {}
      const wsStations = []
      for (const s of stations) {
        const v = Math.min(perStation, s.max_lbs)
        updates[s.index] = v
        wsStations.push({ index: s.index, weight_lbs: v })
      }
      setPayloadWeights(prev => ({ ...prev, ...updates }))
      sendPayload(wsStations)
    } else if (currentTotal > 0) {
      const scale = targetLbs / currentTotal
      const updates = {}
      const wsStations = []
      for (const s of stations) {
        const v = Math.min((payloadWeights[s.index] || 0) * scale, s.max_lbs)
        updates[s.index] = v
        wsStations.push({ index: s.index, weight_lbs: v })
      }
      setPayloadWeights(prev => ({ ...prev, ...updates }))
      sendPayload(wsStations)
    }
  }, [weightConfig, payloadWeights, sendPayload])

  const restoreDefaults = useCallback(() => {
    if (!weightConfig) return
    const pw = {}
    const wsStations = []
    for (const s of weightConfig.payload_stations || []) {
      pw[s.index] = s.default_lbs
      wsStations.push({ index: s.index, weight_lbs: s.default_lbs })
    }
    setPayloadWeights(pw)
    sendPayload(wsStations)
    const fw = {}
    const wsTanks = []
    for (const t of weightConfig.fuel_stations || []) {
      fw[t.tank_index] = t.default_lbs
      wsTanks.push({ index: t.tank_index, quantity_lbs: t.default_lbs })
    }
    setFuelWeights(fw)
    sendFuelLoading(wsTanks)
  }, [weightConfig, sendPayload, sendFuelLoading])

  const conv = (lbs) => unitLbs ? lbs : lbs * LBS_TO_KG
  const unitLabel = unitLbs ? 'lbs' : 'kg'
  const maxTow = weightConfig?.max_takeoff_weight_lbs || 2400

  return (
    <div>
      {/* Unit toggle */}
      <div style={{ display: 'flex', justifyContent: 'flex-end', marginBottom: 8 }}>
        <div style={{ display: 'flex', gap: 2 }}>
          {['lbs', 'kg'].map(u => (
            <button key={u} onClick={() => setUnitLbs(u === 'lbs')} style={{
              padding: '2px 10px', fontSize: 10, fontWeight: 700,
              fontFamily: 'monospace', border: 'none', borderRadius: 2, cursor: 'pointer',
              background: (u === 'lbs') === unitLbs ? '#39d0d8' : '#1e293b',
              color: (u === 'lbs') === unitLbs ? '#0f172a' : '#475569',
            }}>{u.toUpperCase()}</button>
          ))}
        </div>
      </div>

      <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap' }}>
        {/* LEFT — sliders */}
        <div style={{ flex: 1, minWidth: 220 }}>
          {/* Fuel */}
          <SectionHeader title={`FUEL — ${fuelConfig.fuelType}`} />
          {weightConfig && (
            <WbSlider
              label="Total Fuel" value={conv(totalFuel)} max={conv(totalFuelCapacity)}
              unit={unitLabel}
              onChange={v => setTotalFuel(unitLbs ? v : v / LBS_TO_KG)}
            />
          )}
          {weightConfig && (
            <div style={{ paddingLeft: 8, borderLeft: '2px solid #1e293b', marginLeft: 4 }}>
              {(weightConfig.fuel_stations || []).map(t => (
                <WbSlider key={t.tank_index}
                  label={t.label}
                  value={conv(fuelWeights[t.tank_index] || 0)}
                  max={conv(t.capacity_lbs)}
                  unit={unitLabel}
                  onChange={v => updateFuelTank(t.tank_index, unitLbs ? v : v / LBS_TO_KG)}
                />
              ))}
            </div>
          )}
          {!weightConfig && fuel.tanks.map((tank, i) => {
            const cfgTank = fuelConfig.tanks?.[i] || {}
            const name = cfgTank.name || `Tank ${i + 1}`
            const massKg = tank.massKg ?? 0
            const displayQty = unitLbs ? massKg / LBS_TO_KG : massKg
            return (
              <div key={i} style={{ marginBottom: 8 }}>
                <div style={{
                  display: 'flex', justifyContent: 'space-between', fontSize: 12,
                  fontFamily: 'monospace', marginBottom: 2,
                }}>
                  <span style={{ color: '#64748b' }}>{name}</span>
                  <span style={{ color: '#e2e8f0' }}>
                    {displayQty.toFixed(1)} {unitLabel}  {((tank.pct ?? 0) * 100).toFixed(0)}%
                  </span>
                </div>
                <div style={{ height: 8, background: '#1c2333', borderRadius: 2, overflow: 'hidden' }}>
                  <div style={{
                    height: '100%', borderRadius: 2,
                    width: `${(tank.pct ?? 0) * 100}%`,
                    background: (tank.pct ?? 0) < 0.2 ? '#ff3b30' : '#00ff88',
                    transition: 'width 0.3s',
                  }} />
                </div>
              </div>
            )
          })}

          {/* Payload stations */}
          {weightConfig && (
            <>
              <SectionHeader title="PAYLOAD" />
              <WbSlider
                label="Total Payload" value={conv(totalPayload)} max={conv(totalPayloadMax)}
                unit={unitLabel}
                onChange={v => setTotalPayload(unitLbs ? v : v / LBS_TO_KG)}
              />
              <div style={{ paddingLeft: 8, borderLeft: '2px solid #1e293b', marginLeft: 4 }}>
                {(weightConfig.payload_stations || []).map(s => (
                  <WbSlider key={s.index}
                    label={s.label}
                    value={conv(payloadWeights[s.index] || 0)}
                    max={conv(s.max_lbs)}
                    unit={unitLabel}
                    onChange={v => updateStation(s.index, unitLbs ? v : v / LBS_TO_KG)}
                  />
                ))}
              </div>
            </>
          )}

          {/* Restore defaults */}
          {weightConfig && (
            <button onClick={restoreDefaults} style={{
              marginTop: 12, padding: '6px 16px', fontSize: 10, fontWeight: 700,
              fontFamily: 'monospace', letterSpacing: 1, textTransform: 'uppercase',
              background: '#1e293b', color: '#94a3b8', border: '1px solid #334155',
              borderRadius: 4, cursor: 'pointer',
            }}>Restore Defaults</button>
          )}
        </div>

        {/* RIGHT — summary */}
        <div style={{ width: 130, flexShrink: 0 }}>
          <WeightDonut
            current={conv(fdm.totalMassKg / LBS_TO_KG)}
            max={conv(maxTow)}
            unit={unitLabel}
          />
          <div style={{ marginTop: 12, fontSize: 11, fontFamily: 'monospace', textAlign: 'center' }}>
            <div style={{ color: '#64748b', fontSize: 9, letterSpacing: 1, marginBottom: 2 }}>CG POSITION</div>
            <div style={{ color: '#e2e8f0', fontWeight: 700 }}>
              {fdm.cgXIn?.toFixed(1) ?? '--'} in
            </div>
          </div>
          {/* CG envelope chart placeholder — Phase 3 */}
        </div>
      </div>

      <PanelRow label="Aircraft" value={aircraftId.toUpperCase()} />
    </div>
  )
}
