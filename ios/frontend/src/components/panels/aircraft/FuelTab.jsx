import { useState, useEffect, useCallback, useRef } from 'react'
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

// ── CG Envelope Helpers ──────────────────────────────────────────────

function interpolateLimit(polyline, weight) {
  if (!polyline || polyline.length === 0) return null
  if (weight <= polyline[0][0]) return polyline[0][1]
  if (weight >= polyline[polyline.length - 1][0]) return polyline[polyline.length - 1][1]
  for (let i = 0; i < polyline.length - 1; i++) {
    const [w0, a0] = polyline[i]
    const [w1, a1] = polyline[i + 1]
    if (weight >= w0 && weight <= w1) {
      const t = (weight - w0) / (w1 - w0)
      return a0 + t * (a1 - a0)
    }
  }
  return polyline[polyline.length - 1][1]
}

function isInsideEnvelope(arm, weight, envelope) {
  if (!envelope) return true
  const fwd = envelope.forward || []
  const aft = envelope.aft || []
  if (fwd.length === 0 || aft.length === 0) return true
  const minW = Math.min(fwd[0][0], aft[0][0])
  const maxW = Math.max(fwd[fwd.length - 1][0], aft[aft.length - 1][0])
  if (weight < minW || weight > maxW) return false
  const fwdLimit = interpolateLimit(fwd, weight)
  const aftLimit = interpolateLimit(aft, weight)
  return arm >= fwdLimit && arm <= aftLimit
}

function computeCgArm(emptyW, emptyArm, payloadWeights, payloadStations, fuelWeights, fuelStations) {
  let totalMoment = emptyW * emptyArm
  let totalWeight = emptyW
  for (const s of payloadStations) {
    const w = payloadWeights[s.index] || 0
    totalMoment += w * s.arm_in
    totalWeight += w
  }
  for (const t of fuelStations) {
    const w = fuelWeights[t.tank_index] || 0
    totalMoment += w * t.arm_in
    totalWeight += w
  }
  return totalWeight > 0 ? totalMoment / totalWeight : emptyArm
}

function CgEnvelopeChart({ weightConfig, payloadWeights, fuelWeights, unitLbs, fdm }) {
  if (!weightConfig?.cg_envelope) return null

  const env = weightConfig.cg_envelope
  const fwd = env.forward || []
  const aft = env.aft || []
  if (fwd.length === 0 || aft.length === 0) return null

  const emptyW = weightConfig.empty_weight_lbs || 0
  const emptyArm = weightConfig.empty_cg_arm_in || 0
  const mtow = weightConfig.max_takeoff_weight_lbs || 2400
  const mlw = weightConfig.max_landing_weight_lbs || mtow
  const payloadStations = weightConfig.payload_stations || []
  const fuelStations = weightConfig.fuel_stations || []

  // Data ranges
  const allArms = [...fwd.map(p => p[1]), ...aft.map(p => p[1])]
  const allWeights = [...fwd.map(p => p[0]), ...aft.map(p => p[0])]
  const armMin = Math.min(...allArms) - 2
  const armMax = Math.max(...allArms) + 2
  const wMin = Math.min(...allWeights) - 100
  const wMax = Math.max(mtow, ...allWeights) + 100

  // SVG layout
  const pad = { top: 14, right: 14, bottom: 38, left: 50 }
  const vw = 360, vh = 340
  const cw = vw - pad.left - pad.right
  const ch = vh - pad.top - pad.bottom

  const toX = (arm) => pad.left + (arm - armMin) / (armMax - armMin) * cw
  const toY = (w) => pad.top + (1 - (w - wMin) / (wMax - wMin)) * ch
  const conv = (lbs) => unitLbs ? lbs : lbs * LBS_TO_KG
  const unitLabel = unitLbs ? 'lbs' : 'kg'

  // Envelope polygon: forward bottom→top, then aft top→bottom
  const polyPoints = [
    ...fwd.map(([w, a]) => `${toX(a)},${toY(w)}`),
    ...[...aft].reverse().map(([w, a]) => `${toX(a)},${toY(w)}`),
  ].join(' ')

  // Compute local CG
  const payloadTotal = Object.values(payloadWeights).reduce((s, v) => s + v, 0)
  const fuelTotal = Object.values(fuelWeights).reduce((s, v) => s + v, 0)
  const localWeight = emptyW + payloadTotal + fuelTotal
  const localArm = computeCgArm(emptyW, emptyArm, payloadWeights, payloadStations, fuelWeights, fuelStations)
  const localInside = isInsideEnvelope(localArm, localWeight, env)

  // ZFW dot
  const zfwWeight = emptyW + payloadTotal
  const zfwArm = computeCgArm(emptyW, emptyArm, payloadWeights, payloadStations, {}, [])

  // FDM dot
  const fdmWeight = (fdm.totalMassKg || 0) / LBS_TO_KG
  const fdmArm = fdm.cgXIn || 0

  // Axis ticks
  const armStep = (armMax - armMin) > 20 ? 5 : 2
  const wStep = (wMax - wMin) > 1000 ? 200 : 100
  const armTicks = []
  for (let a = Math.ceil(armMin / armStep) * armStep; a <= armMax; a += armStep) armTicks.push(a)
  const wTicks = []
  for (let w = Math.ceil(wMin / wStep) * wStep; w <= wMax; w += wStep) wTicks.push(w)

  return (
    <svg viewBox={`0 0 ${vw} ${vh}`} style={{ width: '100%', maxWidth: 600 }}>
      {/* Grid */}
      {armTicks.map(a => (
        <line key={`ga${a}`} x1={toX(a)} y1={pad.top} x2={toX(a)} y2={pad.top + ch}
          stroke="#1e293b" strokeWidth={0.5} strokeDasharray="2,2" />
      ))}
      {wTicks.map(w => (
        <line key={`gw${w}`} x1={pad.left} y1={toY(w)} x2={pad.left + cw} y2={toY(w)}
          stroke="#1e293b" strokeWidth={0.5} strokeDasharray="2,2" />
      ))}

      {/* Envelope polygon */}
      <polygon points={polyPoints} fill="rgba(0,255,136,0.08)" stroke="#00ff88" strokeWidth={1.5} />

      {/* MTOW line */}
      <line x1={pad.left} y1={toY(mtow)} x2={pad.left + cw} y2={toY(mtow)}
        stroke="#f59e0b" strokeWidth={1} strokeDasharray="4,3" />
      <text x={pad.left + cw + 2} y={toY(mtow) + 3} fill="#f59e0b"
        fontSize={10} fontFamily="monospace">MTOW</text>

      {/* MLW line (if different) */}
      {mlw !== mtow && (
        <>
          <line x1={pad.left} y1={toY(mlw)} x2={pad.left + cw} y2={toY(mlw)}
            stroke="#39d0d8" strokeWidth={1} strokeDasharray="4,3" />
          <text x={pad.left + cw + 2} y={toY(mlw) + 3} fill="#39d0d8"
            fontSize={10} fontFamily="monospace">MLW</text>
        </>
      )}

      {/* Empty weight marker */}
      <circle cx={toX(emptyArm)} cy={toY(emptyW)} r={4} fill="#64748b" />
      <text x={toX(emptyArm) + 5} y={toY(emptyW) + 3} fill="#64748b"
        fontSize={10} fontFamily="monospace">EW</text>

      {/* Loading line: EW → ZFW → live CG */}
      <line x1={toX(emptyArm)} y1={toY(emptyW)} x2={toX(zfwArm)} y2={toY(zfwWeight)}
        stroke="#475569" strokeWidth={0.5} strokeDasharray="2,2" />
      <line x1={toX(zfwArm)} y1={toY(zfwWeight)} x2={toX(localArm)} y2={toY(localWeight)}
        stroke="#475569" strokeWidth={0.5} strokeDasharray="2,2" />

      {/* ZFW dot */}
      {payloadTotal > 0 && (
        <>
          <rect x={toX(zfwArm) - 3} y={toY(zfwWeight) - 3} width={6} height={6}
            fill="#39d0d8" transform={`rotate(45 ${toX(zfwArm)} ${toY(zfwWeight)})`} />
          <text x={toX(zfwArm) + 6} y={toY(zfwWeight) + 3} fill="#39d0d8"
            fontSize={10} fontFamily="monospace">ZFW</text>
        </>
      )}

      {/* FDM confirmed dot (smaller, only if different from local) */}
      {fdmArm > 0 && Math.abs(fdmArm - localArm) > 0.5 && (
        <circle cx={toX(fdmArm)} cy={toY(fdmWeight)} r={3}
          fill="none" stroke="#e2e8f0" strokeWidth={1} />
      )}

      {/* Live CG dot */}
      <circle cx={toX(localArm)} cy={toY(localWeight)} r={7}
        fill={localInside ? '#00ff88' : '#ff3b30'}
        stroke={localInside ? '#00ff8844' : '#ff3b3044'} strokeWidth={3} />

      {/* Axis labels */}
      {armTicks.map(a => (
        <text key={`la${a}`} x={toX(a)} y={pad.top + ch + 12} fill="#64748b"
          fontSize={10} fontFamily="monospace" textAnchor="middle">{a}</text>
      ))}
      {wTicks.map(w => (
        <text key={`lw${w}`} x={pad.left - 4} y={toY(w) + 3} fill="#64748b"
          fontSize={10} fontFamily="monospace" textAnchor="end">{conv(w).toFixed(0)}</text>
      ))}
      <text x={pad.left + cw / 2} y={vh - 2} fill="#475569"
        fontSize={10} fontFamily="monospace" textAnchor="middle">
        CG ({weightConfig.unit_label || 'inches'})
      </text>
      <text x={4} y={pad.top + ch / 2} fill="#475569"
        fontSize={10} fontFamily="monospace" textAnchor="middle"
        transform={`rotate(-90 4 ${pad.top + ch / 2})`}>
        {unitLabel}
      </text>
    </svg>
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
  const [fuelDirty, setFuelDirty] = useState(false)
  const fuelDirtyTimer = useRef(null)

  useEffect(() => () => clearTimeout(fuelDirtyTimer.current), [])

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

  const markFuelDirty = useCallback(() => {
    setFuelDirty(true)
    clearTimeout(fuelDirtyTimer.current)
    fuelDirtyTimer.current = setTimeout(() => setFuelDirty(false), 2000)
  }, [])

  // Sync fuel weights from solver when sim is RUNNING and user isn't dragging
  useEffect(() => {
    if (fuelDirty || simState !== 'RUNNING' || !weightConfig) return
    const fw = {}
    const KG_TO_LBS = 1 / LBS_TO_KG
    fuel.tanks.forEach((tank, i) => {
      const fuelStation = (weightConfig.fuel_stations || [])[i]
      if (fuelStation) {
        fw[fuelStation.tank_index] = (tank.massKg ?? 0) * KG_TO_LBS
      }
    })
    if (Object.keys(fw).length > 0) setFuelWeights(fw)
  }, [fuelDirty, simState, fuel.tanks, weightConfig])

  const updateStation = useCallback((index, lbs) => {
    setPayloadWeights(prev => ({ ...prev, [index]: lbs }))
    sendPayload([{ index, weight_lbs: lbs }])
  }, [sendPayload])

  const updateFuelTank = useCallback((tankIndex, lbs) => {
    markFuelDirty()
    setFuelWeights(prev => {
      const next = { ...prev, [tankIndex]: lbs }
      const tanks = Object.entries(next).map(([idx, qty]) => ({
        index: parseInt(idx), quantity_lbs: qty
      }))
      sendFuelLoading(tanks)
      return next
    })
  }, [sendFuelLoading, markFuelDirty])

  const totalFuelCapacity = (weightConfig?.fuel_stations || []).reduce((s, t) => s + t.capacity_lbs, 0)
  const totalFuel = Object.values(fuelWeights).reduce((s, v) => s + v, 0)

  const setTotalFuel = useCallback((targetLbs) => {
    markFuelDirty()
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
  }, [weightConfig, totalFuelCapacity, sendFuelLoading, markFuelDirty])

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
  const emptyWeight = weightConfig?.empty_weight_lbs || 0
  const localTotalWeight = emptyWeight + totalPayload + totalFuel
  const emptyArm = weightConfig?.empty_cg_arm_in || 0
  const localCgArm = computeCgArm(
    emptyWeight, emptyArm, payloadWeights,
    weightConfig?.payload_stations || [], fuelWeights,
    weightConfig?.fuel_stations || [])
  const localInsideEnvelope = isInsideEnvelope(localCgArm, localTotalWeight, weightConfig?.cg_envelope)

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

      <div style={{ display: 'flex', gap: 16 }}>
        {/* LEFT — sliders */}
        <div style={{ flex: 1, minWidth: 180, maxWidth: '50%' }}>
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

        {/* RIGHT — summary + CG chart */}
        <div style={{ flex: 1, minWidth: 200 }}>
          <div style={{ display: 'flex', gap: 12, alignItems: 'flex-start' }}>
            <WeightDonut
              current={conv(localTotalWeight)}
              max={conv(maxTow)}
              unit={unitLabel}
            />
            <div style={{ fontSize: 11, fontFamily: 'monospace' }}>
              <div style={{ color: '#64748b', fontSize: 9, letterSpacing: 1, marginBottom: 2 }}>CG</div>
              <div style={{ color: '#e2e8f0', fontWeight: 700 }}>
                {localCgArm.toFixed(1)} in
              </div>
              <div style={{ color: '#475569', fontSize: 9, marginTop: 4 }}>
                FDM: {conv(fdm.totalMassKg / LBS_TO_KG).toFixed(0)} {unitLabel}
              </div>
              <div style={{
                color: localInsideEnvelope ? '#00ff88' : '#ff3b30',
                fontSize: 9, fontWeight: 700, marginTop: 4,
              }}>
                {localInsideEnvelope ? 'IN ENVELOPE' : 'OUT OF ENVELOPE'}
              </div>
            </div>
          </div>
          {weightConfig?.cg_envelope && (
            <div style={{ marginTop: 8 }}>
              <CgEnvelopeChart
                weightConfig={weightConfig}
                payloadWeights={payloadWeights}
                fuelWeights={fuelWeights}
                unitLbs={unitLbs}
                fdm={fdm}
              />
            </div>
          )}
        </div>
      </div>

      <PanelRow label="Aircraft" value={aircraftId.toUpperCase()} />
    </div>
  )
}
