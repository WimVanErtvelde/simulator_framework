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
import ToggleSwitch from './components/ToggleSwitch'
import ControlPositionIndicator from './components/ControlPositionIndicator'
import SelectorControl from './components/SelectorControl'
import VerticalSlider from './components/VerticalSlider'

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

      {/* ── SWITCH PANEL (full-width horizontal row) ────────────── */}
      <Section title="SWITCHES">
        <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap', justifyContent: 'center', alignItems: 'flex-end' }}>
          <ToggleSwitch label="BATT" on={sw('sw_battery')}
            onToggle={() => { if (!isForced('sw_battery')) sendVirtualPanel(['sw_battery'], [!sw('sw_battery')]) }} />
          <ToggleSwitch label="ALT" on={sw('sw_alt')}
            onToggle={() => { if (!isForced('sw_alt')) sendVirtualPanel(['sw_alt'], [!sw('sw_alt')]) }} />
          <ToggleSwitch label="AVIONICS" on={sw('sw_avionics_master')}
            onToggle={() => { if (!isForced('sw_avionics_master')) sendVirtualPanel(['sw_avionics_master'], [!sw('sw_avionics_master')]) }} />
          <ToggleSwitch label="FUEL PUMP" on={sw('sw_fuel_pump')}
            onToggle={() => { if (!isForced('sw_fuel_pump')) sendVirtualPanel(['sw_fuel_pump'], [!sw('sw_fuel_pump')]) }} />
          <ToggleSwitch label="PITOT HT" on={sw('sw_pitot_heat')}
            onToggle={() => { if (!isForced('sw_pitot_heat')) sendVirtualPanel(['sw_pitot_heat'], [!sw('sw_pitot_heat')]) }} />
          <ToggleSwitch label="BCN" on={sw('sw_beacon')}
            onToggle={() => { if (!isForced('sw_beacon')) sendVirtualPanel(['sw_beacon'], [!sw('sw_beacon')]) }} />
          <ToggleSwitch label="NAV" on={sw('sw_nav_lt')}
            onToggle={() => { if (!isForced('sw_nav_lt')) sendVirtualPanel(['sw_nav_lt'], [!sw('sw_nav_lt')]) }} />
          <ToggleSwitch label="STROBE" on={sw('sw_strobe')}
            onToggle={() => { if (!isForced('sw_strobe')) sendVirtualPanel(['sw_strobe'], [!sw('sw_strobe')]) }} />
          <ToggleSwitch label="LAND" on={sw('sw_landing_lt')}
            onToggle={() => { if (!isForced('sw_landing_lt')) sendVirtualPanel(['sw_landing_lt'], [!sw('sw_landing_lt')]) }} />
          <ToggleSwitch label="TAXI" on={sw('sw_taxi_lt')}
            onToggle={() => { if (!isForced('sw_taxi_lt')) sendVirtualPanel(['sw_taxi_lt'], [!sw('sw_taxi_lt')]) }} />
          <div style={{ borderLeft: '1px solid #1e293b', height: 56, margin: '0 4px' }} />
          <ToggleSwitch label="CARB HT" on={sw('sw_carb_heat')}
            onToggle={() => { if (!isForced('sw_carb_heat')) sendVirtualPanel(['sw_carb_heat'], [!sw('sw_carb_heat')]) }} />
          <div style={{ borderLeft: '1px solid #1e293b', height: 56, margin: '0 4px' }} />
          <SelectorControl label="MAGNETOS" value={magnetoPos}
            options={[
              { value: 0, label: 'OFF' }, { value: 1, label: 'R' },
              { value: 2, label: 'L' }, { value: 3, label: 'BOTH' },
              { value: 4, label: 'START' },
            ]}
            onChange={(v) => {
              setMagnetoPos(v)
              kbSetMagneto(v)  // update shared state so keyboard tick doesn't overwrite
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
          <div style={{ textAlign: 'center', marginTop: 8, fontSize: 10 }}>
            <span style={{ color: '#64748b' }}>PARKING BRK</span>{' '}
            <span style={{ color: gear.parkingBrake ? '#ff3b30' : '#22c55e' }}>
              {gear.parkingBrake ? 'SET' : 'OFF'}
            </span>
          </div>
        </Section>

        {/* NAV info summary */}
        <Section title="NAV INFO" style={{ flex: 1, minWidth: 200 }}>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 4, fontSize: 11 }}>
            <div><span style={{ color: '#64748b' }}>COM1</span> {(nav.com1Mhz || 0).toFixed(3)}</div>
            <div><span style={{ color: '#64748b' }}>COM2</span> {(nav.com2Mhz || 0).toFixed(3)}</div>
            <div><span style={{ color: '#64748b' }}>NAV1</span> {(nav.nav1Mhz || 0).toFixed(2)} {nav.nav1Ident || ''}</div>
            <div><span style={{ color: '#64748b' }}>NAV2</span> {(nav.nav2Mhz || 0).toFixed(2)} {nav.nav2Ident || ''}</div>
            <div><span style={{ color: '#64748b' }}>ADF</span> {(nav.adf1Khz || 0).toFixed(0)} kHz</div>
            <div><span style={{ color: '#64748b' }}>DME</span> {nav.dmeValid ? `${nav.dmeDistanceNm?.toFixed(1)} NM` : '---'}</div>
            <div><span style={{ color: '#64748b' }}>XPDR</span> {nav.xpdrCode?.toString().padStart(4, '0')}</div>
            <div><span style={{ color: '#64748b' }}>OBS1</span> {(nav.nav1ObsDeg || 0).toFixed(0)}°</div>
            <div><span style={{ color: '#64748b' }}>HDG</span> {hdgMagDeg.toFixed(0)}°M</div>
            <div><span style={{ color: '#64748b' }}>VAR</span> {(nav.magVariationDeg || 0).toFixed(1)}°</div>
          </div>
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
        <span>T/G trim</span> <span>B brake</span> <span>SPACE center</span>
      </div>
    </div>
  )
}
