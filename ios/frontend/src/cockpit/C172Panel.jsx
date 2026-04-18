import { useState } from 'react'
import { useSimStore } from '../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import useKeyboardControls from './useKeyboardControls'

// Flight instruments
import ASI from './instruments/ASI'
import Altimeter from './instruments/Altimeter'
import VSI from './instruments/VSI'
import HeadingIndicator from './instruments/HeadingIndicator'
import AttitudeIndicator from './instruments/AttitudeIndicator'
import TurnCoordinator from './instruments/TurnCoordinator'

// Components
import RoundGauge from './components/RoundGauge'
import BarGauge from './components/BarGauge'
import AnnunciatorLight from './components/AnnunciatorLight'
import ControlPositionIndicator from './components/ControlPositionIndicator'
import SelectorControl from './components/SelectorControl'
import VerticalSlider from './components/VerticalSlider'

// Embeddable sections
import ElectricalSection from './sections/ElectricalSection'
import RadioSection from './sections/RadioSection'

const BG = '#0a0e14'
const PANEL = '#111827'
const BORDER = '#1e293b'
const FONT = "'JetBrains Mono', 'Fira Code', monospace"

function Section({ title, children, style }) {
  return (
    <div style={{
      background: PANEL, border: `1px solid ${BORDER}`, borderRadius: 6,
      padding: 10, ...style,
    }}>
      {title && <div style={{ color: '#64748b', fontSize: 9, fontFamily: FONT, marginBottom: 6,
        letterSpacing: 1, fontWeight: 600 }}>{title}</div>}
      {children}
    </div>
  )
}

// Brake visual: compact pill showing keyboard shortcut, label, and active state.
// Two are used in the CONTROLS section — PARK (P, latched) and BRAKE (B, held).
function BrakeIndicator({ label, shortcut, active, activeColor = '#ff3b30' }) {
  return (
    <div style={{
      display: 'flex', flexDirection: 'column', alignItems: 'center',
      gap: 3, padding: '6px 10px',
      background: active ? `${activeColor}22` : '#0a0e14',
      border: `1px solid ${active ? activeColor : '#1e293b'}`,
      borderRadius: 4, minWidth: 48,
      fontFamily: FONT,
    }}>
      <div style={{
        color: active ? activeColor : '#475569',
        fontSize: 14, fontWeight: 700, lineHeight: 1,
      }}>{shortcut}</div>
      <div style={{
        color: active ? activeColor : '#64748b',
        fontSize: 8, letterSpacing: 1, lineHeight: 1,
      }}>{label}</div>
      <div style={{
        width: 8, height: 8, borderRadius: '50%',
        background: active ? activeColor : '#1e293b',
        boxShadow: active ? `0 0 4px ${activeColor}` : 'none',
      }} />
    </div>
  )
}

function sendVirtualPanel(switchIds, switchStates, selectorIds, selectorValues) {
  const ws = useSimStore.getState().ws
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    console.warn('[C172Panel] WS not connected, cannot send panel command')
    return
  }
  const data = {}
  if (switchIds?.length) { data.switch_ids = switchIds; data.switch_states = switchStates }
  if (selectorIds?.length) { data.selector_ids = selectorIds; data.selector_values = selectorValues }
  const msg = JSON.stringify({ type: 'set_virtual_panel', data })
  console.log('[C172Panel] TX:', msg)
  ws.send(msg)
}

function sendEngineControls(data) {
  const ws = useSimStore.getState().ws
  if (!ws || ws.readyState !== WebSocket.OPEN) return
  ws.send(JSON.stringify({ type: 'set_engine_controls', data }))
}



export default function C172Panel() {
  const { fdm, airData, nav, electrical, engines, fuel, gear, atmosphere, forcedSwitchIds } = useSimStore(useShallow(s => ({
    fdm: s.fdm, airData: s.airData, nav: s.nav, electrical: s.electrical,
    engines: s.engines, fuel: s.fuel, gear: s.gear, atmosphere: s.atmosphere,
    forcedSwitchIds: s.forcedSwitchIds ?? [],
  })))
  const { state: kb, setThrottle, setMixture, setMagneto: kbSetMagneto } = useKeyboardControls()
  const [magnetoPos, setMagnetoPos] = useState(0)
  const [fuelSelPos, setFuelSelPos] = useState(0)

  // Guard: skip clicks on forced switches
  const isForced = (id) => forcedSwitchIds.includes(id)

  // Helper to find switch state by ID
  const sw = (id) => {
    const idx = electrical.switchIds.indexOf(id)
    return idx >= 0 ? electrical.switchClosed[idx] : false
  }

  const hdgMagDeg = (nav.hdgMagDeg != null && nav.hdgMagDeg !== 0) ? nav.hdgMagDeg : fdm.hdgTrueDeg

  return (
    <div style={{
      background: BG, minHeight: '100vh', padding: 8,
      fontFamily: FONT, color: '#e2e8f0',
      display: 'flex', flexDirection: 'column', gap: 8,
    }}>
      {/* ── ANNUNCIATOR STRIP ──────────────────────────────────────── */}
      <div style={{ display: 'flex', gap: 6, justifyContent: 'center', flexWrap: 'wrap' }}>
        <AnnunciatorLight label="LOW VOLTS" active={electrical.masterBusVoltage < 24} color="#f59e0b" />
        <AnnunciatorLight label="LOW OIL" active={engines.lowOilPressureWarning?.[0]} color="#ff3b30" />
        <AnnunciatorLight label="LOW FUEL" active={fuel.lowFuelWarning} color="#f59e0b" />
        <AnnunciatorLight label="VACUUM" active={false} color="#f59e0b" />
        <AnnunciatorLight label="PITOT HT" active={!airData.pitotHeatOn && !gear.onGround} color="#f59e0b" />
      </div>

      {/* ── SWITCH PANEL (from ElectricalSection) ────────────────── */}
      <Section title="SWITCHES">
        <ElectricalSection compact />
        {/* Magneto selector stays here — not part of electrical config */}
        <div style={{ display: 'flex', gap: 6, justifyContent: 'center', marginTop: 6 }}>
          <SelectorControl label="MAGNETOS" value={magnetoPos}
            options={[
              { value: 0, label: 'OFF' }, { value: 1, label: 'R' },
              { value: 2, label: 'L' }, { value: 3, label: 'BOTH' },
              { value: 4, label: 'START' },
            ]}
            onChange={(v) => {
              setMagnetoPos(v)
              kbSetMagneto(v)
              sendVirtualPanel(['sw_starter_engage'], [v === 4], ['sel_magnetos'], [v])
            }} />
        </div>
      </Section>

      {/* ── CIRCUIT BREAKER PANEL (horizontal, like real C172) ───── */}
      {electrical.cbNames?.length > 0 && (
        <Section title="CIRCUIT BREAKERS">
          <div style={{ display: 'flex', gap: 4, flexWrap: 'wrap', justifyContent: 'center' }}>
            {electrical.cbNames.map((name, i) => {
              const closed = electrical.cbClosed?.[i]
              const tripped = electrical.cbTripped?.[i]
              const label = name.replace('cb_', '').replace(/_/g, ' ').toUpperCase()
              return (
                <div key={name}
                  onClick={() => { if (!isForced(name)) sendVirtualPanel([name], [!closed]) }}
                  title={`${name}: ${tripped ? 'TRIPPED' : closed ? 'IN' : 'POPPED'} — click to ${closed ? 'pull' : 'reset'}`}
                  style={{
                    width: 40, textAlign: 'center', cursor: 'pointer', userSelect: 'none',
                    padding: '4px 2px', borderRadius: 4,
                    background: tripped ? '#3b1111' : '#0d1117',
                    border: `1px solid ${tripped ? '#ff3b30' : closed ? '#1e293b' : '#f59e0b'}`,
                  }}
                >
                  {/* CB dot indicator */}
                  <div style={{
                    width: 10, height: 10, borderRadius: '50%', margin: '0 auto 3px',
                    background: tripped ? '#ff3b30' : closed ? '#00ff88' : '#f59e0b',
                    boxShadow: `0 0 4px ${tripped ? '#ff3b30' : closed ? '#00ff8844' : '#f59e0b44'}`,
                  }} />
                  <div style={{
                    fontSize: 7, fontFamily: FONT, color: '#94a3b8',
                    lineHeight: 1.1, wordBreak: 'break-word',
                  }}>
                    {label}
                  </div>
                </div>
              )
            })}
          </div>
        </Section>
      )}

      {/* ── MAIN PANEL ────────────────────────────────────────────── */}
      <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>

        {/* CENTER: Flight Instruments (6-pack) */}
        <Section title="FLIGHT INSTRUMENTS" style={{ flex: 1 }}>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 2, justifyItems: 'center' }}>
            <div style={{ transform: 'scale(0.78)', transformOrigin: 'top center' }}><ASI iasKt={airData.iasKt} /></div>
            <div style={{ transform: 'scale(0.78)', transformOrigin: 'top center' }}><AttitudeIndicator pitchDeg={fdm.pitchDeg} rollDeg={fdm.rollDeg} /></div>
            <div style={{ transform: 'scale(0.78)', transformOrigin: 'top center' }}><Altimeter altFt={airData.altIndicatedFt} qnhHpa={atmosphere.qnhHpa} /></div>
            <div style={{ transform: 'scale(0.78)', transformOrigin: 'top center', marginTop: -30 }}><TurnCoordinator yawRateRads={0} rollDeg={fdm.rollDeg} /></div>
            <div style={{ transform: 'scale(0.78)', transformOrigin: 'top center', marginTop: -30 }}><HeadingIndicator hdgDeg={hdgMagDeg} /></div>
            <div style={{ transform: 'scale(0.78)', transformOrigin: 'top center', marginTop: -30 }}><VSI vsFpm={airData.vsFpm} /></div>
          </div>
        </Section>

        {/* RIGHT: Engine Gauges */}
        <Section title="ENGINE" style={{ minWidth: 200 }}>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: 4, justifyItems: 'center' }}>
            <RoundGauge value={engines.rpm?.[0] ?? 0} min={0} max={3000}
              label="TACH" unit="RPM" greenArc={[2100, 2700]} redLine={2700}
              ticks={6} decimals={0} size={110} />
            <RoundGauge value={engines.manifoldPressureInhg?.[0] ?? 0} min={10} max={35}
              label="MAN PRESS" unit="inHg" ticks={5} decimals={1} size={110} />
            <RoundGauge value={engines.oilPressurePsi?.[0] ?? 0} min={0} max={100}
              label="OIL PRESS" unit="PSI" greenArc={[60, 90]} redLine={25}
              ticks={5} decimals={0} size={110} />
            <RoundGauge value={engines.oilTempDegc?.[0] ?? 0} min={0} max={130}
              label="OIL TEMP" unit="C" greenArc={[50, 100]} redLine={116}
              ticks={5} decimals={0} size={110} />
            <RoundGauge value={engines.egtDegc?.[0] ?? 0} min={0} max={900}
              label="EGT" unit="C" ticks={6} decimals={0} size={110} />
            <RoundGauge value={engines.chtDegc?.[0] ?? 0} min={0} max={260}
              label="CHT" unit="C" greenArc={[100, 240]} redLine={238}
              ticks={5} decimals={0} size={110} />
          </div>
          {/* Electrical mini */}
          <div style={{ display: 'flex', gap: 12, justifyContent: 'center', marginTop: 8 }}>
            <RoundGauge value={electrical.masterBusVoltage} min={0} max={30}
              label="VOLTS" unit="V" greenArc={[24, 29]} ticks={6} decimals={1} size={100} />
            <RoundGauge value={electrical.totalLoadAmps} min={0} max={60}
              label="AMPS" unit="A" ticks={6} decimals={0} size={100} />
          </div>
        </Section>
      </div>

      {/* ── LOWER PANEL ───────────────────────────────────────────── */}
      <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>

        {/* Control Position */}
        <Section title="CONTROLS" style={{ minWidth: 130 }}>
          <ControlPositionIndicator aileron={kb.aileron} elevator={kb.elevator} rudder={kb.rudder} />
          <div style={{ display: 'flex', gap: 6, marginTop: 8, justifyContent: 'center' }}>
            {/* Read from local keyboard state (kb) — immediate feedback, doesn't
                depend on the backend→arbitrator→gear echo loop. Stays in sync
                with what the keyboard handler is actually sending. */}
            <BrakeIndicator label="PARK" shortcut="P" active={kb.parkingBrake}
              activeColor="#ff3b30" />
            <BrakeIndicator label="BRAKE" shortcut="B" active={kb.brakeHeld}
              activeColor="#f59e0b" />
          </div>
        </Section>

        {/* Engine Controls */}
        <Section title="ENGINE" style={{ minWidth: 120 }}>
          <div style={{ display: 'flex', gap: 16, justifyContent: 'center' }}>
            <VerticalSlider label="THR" color="#333" value={kb.throttle} height={100}
              onChange={(v) => setThrottle(v)} />
            <VerticalSlider label="MIX" color="#cc2222" value={kb.mixture} height={100}
              onChange={(v) => setMixture(v)} />
          </div>
        </Section>

        {/* Fuel */}
        <Section title="FUEL" style={{ minWidth: 160 }}>
          <div style={{ display: 'flex', gap: 16, justifyContent: 'center' }}>
            {fuel.tanks?.map((t, i) => (
              <BarGauge key={i} value={t.liters ?? 0} max={(t.massKg ?? 92) / 0.72}
                label={t.id ?? `Tank ${i}`} unit="L" decimals={0}
                height={80} warnBelow={12} />
            ))}
          </div>
          <div style={{ marginTop: 6 }}>
            <SelectorControl label="FUEL SEL"
              options={[
                { value: 3, label: 'OFF' }, { value: 1, label: 'LEFT' },
                { value: 0, label: 'BOTH' }, { value: 2, label: 'RIGHT' },
              ]}
              value={fuelSelPos}
              onChange={(v) => { setFuelSelPos(v); sendVirtualPanel(null, null, ['sel_fuel'], [v]) }} />
          </div>
          <div style={{ textAlign: 'center', marginTop: 4, fontSize: 10, color: '#64748b' }}>
            Total: {fuel.totalFuelLiters?.toFixed(0) ?? '--'} L ({((fuel.totalFuelPct ?? 0) * 100).toFixed(0)}%)
          </div>
        </Section>

        {/* Fuel flow + misc */}
        <Section title="FLOW / TRIM" style={{ minWidth: 120 }}>
          <RoundGauge value={fuel.engineFuelFlowLph?.[0] ? fuel.engineFuelFlowLph[0] / 0.72 / 3.785 : 0}
            min={0} max={16} label="FUEL FLOW" unit="GPH" ticks={4} decimals={1} size={120} />
        </Section>

        {/* RADIOS — virtual cockpit tuning with power gating */}
        <Section title="RADIOS" style={{ width: 200 }}>
          <RadioSection compact />
        </Section>
      </div>

      {/* ── DIAGNOSTIC (temporary) ─────────────────────────────── */}
      <div style={{ fontSize: 9, color: '#475569', fontFamily: FONT, padding: 4, background: '#0d1117', borderRadius: 4 }}>
        BUS: {electrical.masterBusVoltage?.toFixed(1)}V | AMPS: {electrical.totalLoadAmps?.toFixed(1)} |
        SOC: {electrical.batterySocPct?.toFixed(0)}% |
        SW: [{electrical.switchIds?.join(', ')}] |
        CLOSED: [{electrical.switchClosed?.map(c => c ? '1' : '0').join('')}] |
        SRC: [{electrical.sourceNames?.join(', ')}] active=[{electrical.sourceActive?.map(a => a ? '1' : '0').join('')}]
      </div>

      {/* ── KEYBOARD LEGEND ─────────────────────────────────────── */}
      <div style={{
        display: 'flex', gap: 16, justifyContent: 'center', flexWrap: 'wrap',
        padding: 6, fontSize: 9, color: '#475569', fontFamily: FONT,
      }}>
        <span>W/S pitch</span> <span>A/D roll</span> <span>Q/E yaw</span>
        <span>R/F throttle</span> <span>Shift+R/F mixture</span>
        <span>T/G trim</span> <span>B brake (held)</span> <span>P park brake (toggle)</span>
        <span>SPACE center</span>
      </div>
    </div>
  )
}
