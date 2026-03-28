import { useSimStore } from '../store/useSimStore'

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
  if (!ws || ws.readyState !== WebSocket.OPEN) return
  const data = {}
  if (switchIds?.length) { data.switch_ids = switchIds; data.switch_states = switchStates }
  if (selectorIds?.length) { data.selector_ids = selectorIds; data.selector_values = selectorValues }
  ws.send(JSON.stringify({ type: 'set_virtual_panel', data }))
}

function sendEngineControls(throttle, mixture) {
  const ws = useSimStore.getState().ws
  if (!ws || ws.readyState !== WebSocket.OPEN) return
  ws.send(JSON.stringify({
    type: 'set_engine_controls',
    data: { throttle_norm: [throttle], mixture_norm: [mixture] },
  }))
}

export default function C172Panel() {
  const { fdm, airData, nav, electrical, engines, fuel, gear, atmosphere } = useSimStore()

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

      {/* ── MAIN PANEL ────────────────────────────────────────────── */}
      <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>

        {/* LEFT: Switch Panel */}
        <Section title="SWITCHES" style={{ minWidth: 100 }}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, alignItems: 'center' }}>
            <ToggleSwitch label="BATT" on={sw('sw_battery')}
              onToggle={() => sendVirtualPanel(['sw_battery'], [!sw('sw_battery')])} />
            <ToggleSwitch label="ALT" on={sw('sw_alt')}
              onToggle={() => sendVirtualPanel(['sw_alt'], [!sw('sw_alt')])} />
            <ToggleSwitch label="AVIONICS" on={sw('sw_avionics_master')}
              onToggle={() => sendVirtualPanel(['sw_avionics_master'], [!sw('sw_avionics_master')])} />
            <ToggleSwitch label="FUEL PUMP" on={sw('sw_fuel_pump')}
              onToggle={() => sendVirtualPanel(['sw_fuel_pump'], [!sw('sw_fuel_pump')])} />
            <ToggleSwitch label="PITOT HT" on={sw('sw_pitot_heat')}
              onToggle={() => sendVirtualPanel(['sw_pitot_heat'], [!sw('sw_pitot_heat')])} />
            <ToggleSwitch label="BCN" on={sw('sw_beacon')}
              onToggle={() => sendVirtualPanel(['sw_beacon'], [!sw('sw_beacon')])} />
            <ToggleSwitch label="NAV" on={sw('sw_nav_lt')}
              onToggle={() => sendVirtualPanel(['sw_nav_lt'], [!sw('sw_nav_lt')])} />
            <ToggleSwitch label="STROBE" on={sw('sw_strobe')}
              onToggle={() => sendVirtualPanel(['sw_strobe'], [!sw('sw_strobe')])} />
            <ToggleSwitch label="LAND" on={sw('sw_landing_lt')}
              onToggle={() => sendVirtualPanel(['sw_landing_lt'], [!sw('sw_landing_lt')])} />
            <ToggleSwitch label="TAXI" on={sw('sw_taxi_lt')}
              onToggle={() => sendVirtualPanel(['sw_taxi_lt'], [!sw('sw_taxi_lt')])} />
          </div>
          <div style={{ marginTop: 8 }}>
            <SelectorControl label="MAGNETOS" value={0}
              options={[
                { value: 0, label: 'OFF' }, { value: 1, label: 'R' },
                { value: 2, label: 'L' }, { value: 3, label: 'BOTH' },
                { value: 4, label: 'START' },
              ]}
              onChange={(v) => sendVirtualPanel(null, null, ['sel_magnetos'], [v])} />
          </div>
        </Section>

        {/* CENTER: Flight Instruments (6-pack) */}
        <Section title="FLIGHT INSTRUMENTS" style={{ flex: 1 }}>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 4, justifyItems: 'center' }}>
            <ASI iasKt={airData.iasKt} />
            <AttitudeIndicator pitchDeg={fdm.pitchDeg} rollDeg={fdm.rollDeg} />
            <Altimeter altFt={airData.altIndicatedFt} qnhHpa={atmosphere.qnhHpa} />
            <TurnCoordinator yawRateRads={0} rollDeg={fdm.rollDeg} />
            <HeadingIndicator hdgDeg={hdgMagDeg} />
            <VSI vsFpm={airData.vsFpm} />
          </div>
        </Section>

        {/* RIGHT: Engine Gauges */}
        <Section title="ENGINE" style={{ minWidth: 200 }}>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: 4, justifyItems: 'center' }}>
            <RoundGauge value={engines.rpm?.[0] ?? 0} min={0} max={3000}
              label="TACH" unit="RPM" greenArc={[2100, 2700]} redLine={2700}
              ticks={6} decimals={0} size={130} />
            <RoundGauge value={engines.manifoldPressureInhg?.[0] ?? 0} min={10} max={35}
              label="MAN PRESS" unit="inHg" ticks={5} decimals={1} size={130} />
            <RoundGauge value={engines.oilPressurePsi?.[0] ?? 0} min={0} max={100}
              label="OIL PRESS" unit="PSI" greenArc={[60, 90]} redLine={25}
              ticks={5} decimals={0} size={130} />
            <RoundGauge value={engines.oilTempDegc?.[0] ?? 0} min={0} max={130}
              label="OIL TEMP" unit="C" greenArc={[50, 100]} redLine={116}
              ticks={5} decimals={0} size={130} />
            <RoundGauge value={engines.egtDegc?.[0] ?? 0} min={0} max={900}
              label="EGT" unit="C" ticks={6} decimals={0} size={130} />
            <RoundGauge value={engines.chtDegc?.[0] ?? 0} min={0} max={260}
              label="CHT" unit="C" greenArc={[100, 240]} redLine={238}
              ticks={5} decimals={0} size={130} />
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

        {/* Engine Controls */}
        <Section title="ENGINE CONTROLS" style={{ minWidth: 120 }}>
          <div style={{ display: 'flex', gap: 16, justifyContent: 'center' }}>
            <VerticalSlider label="THR" color="#333" value={0} height={140}
              onChange={(v) => sendEngineControls(v, 1.0)} />
            <VerticalSlider label="MIX" color="#cc2222" value={1.0} height={140}
              onChange={(v) => sendEngineControls(0.1, v)} />
          </div>
          <div style={{ display: 'flex', gap: 4, justifyContent: 'center', marginTop: 8 }}>
            <ToggleSwitch label="CARB HT" on={sw('sw_carb_heat')}
              onToggle={() => sendVirtualPanel(['sw_carb_heat'], [!sw('sw_carb_heat')])} />
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
              value={0}
              onChange={(v) => sendVirtualPanel(null, null, ['sel_fuel'], [v])} />
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
    </div>
  )
}
