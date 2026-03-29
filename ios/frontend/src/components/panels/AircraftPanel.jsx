import { useState, useCallback } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { PanelRow, SectionHeader } from './PanelUtils'
import NumpadPopup from '../ui/NumpadPopup'

// ── Shared UI components ────────────────────────────────────────────────────

function DeviationBar({ dots, maxDots = 2.5, label, valid, color = '#00ff88' }) {
  const pct = valid ? 50 + (dots / maxDots) * 50 : 50
  const clamped = Math.max(2, Math.min(98, pct))
  return (
    <div style={{ marginBottom: 6 }}>
      {label && (
        <div style={{ fontSize: 10, color: '#64748b', fontFamily: 'monospace', marginBottom: 2 }}>
          {label}
        </div>
      )}
      <div style={{
        position: 'relative', height: 12, background: '#1c2333',
        borderRadius: 2, overflow: 'hidden',
      }}>
        <div style={{
          position: 'absolute', left: '50%', top: 0, bottom: 0, width: 1,
          background: '#334155',
        }} />
        {[-2, -1, 1, 2].map(d => (
          <div key={d} style={{
            position: 'absolute',
            left: `${50 + (d / maxDots) * 50}%`,
            top: 3, width: 4, height: 4, borderRadius: '50%',
            background: '#334155', transform: 'translateX(-50%)',
          }} />
        ))}
        {valid ? (
          <div style={{
            position: 'absolute', left: `${clamped}%`, top: 1, width: 6, height: 10,
            borderRadius: 1, background: color,
            transform: 'translateX(-50%)', transition: 'left 0.15s',
          }} />
        ) : (
          <div style={{
            position: 'absolute', inset: 0, display: 'flex', alignItems: 'center',
            justifyContent: 'center', fontSize: 9, color: '#ef4444',
            fontFamily: 'monospace', fontWeight: 700, letterSpacing: 1,
          }}>NO SIG</div>
        )}
      </div>
    </div>
  )
}

function NavFlag({ text, active, color = '#ef4444' }) {
  return (
    <span style={{
      display: 'inline-block', fontSize: 10, fontWeight: 700,
      fontFamily: 'monospace', letterSpacing: 1, padding: '1px 4px',
      borderRadius: 2, marginLeft: 4,
      background: active ? color : '#1c2333',
      color: active ? '#0f172a' : '#334155',
    }}>{text}</span>
  )
}

function NoSignal({ text = 'NO SIGNAL' }) {
  return (
    <div style={{
      color: '#ef4444', fontSize: 12, fontFamily: 'monospace',
      fontWeight: 700, padding: '8px 0',
    }}>{text}</div>
  )
}

const inputStyle = {
  width: 72, padding: '2px 4px', fontSize: 12, fontFamily: 'monospace',
  background: '#1c2333', color: '#e2e8f0', border: '1px solid #334155',
  borderRadius: 2, textAlign: 'right',
}

function FreqInput({ label, value, unit, step = 0.05, min, max, onChange, radioType }) {
  const [open, setOpen] = useState(false)
  const [error, setError] = useState(false)
  const decimals = (unit === 'kHz' || unit === '\u00B0' || unit === '') ? 0 : 2
  const isXpdr = radioType === 'xpdr'
  const isObs = radioType === 'obs'

  const handleSubmit = useCallback((val) => {
    if (isXpdr) {
      if (!/^[0-7]{4}$/.test(val)) { setError(true); return }
      onChange(parseInt(val, 10))
    } else {
      const num = parseFloat(val)
      if (isNaN(num) || (min !== undefined && num < min) || (max !== undefined && num > max)) {
        setError(true)
        return
      }
      onChange(num)
    }
    setOpen(false)
  }, [min, max, onChange, isXpdr])

  const displayVal = isXpdr
    ? String(value).padStart(4, '0')
    : typeof value === 'number' ? value.toFixed(decimals) : String(value)

  const hint = isXpdr ? '0000\u20137777'
    : isObs ? '0\u2013360'
    : (min != null && max != null) ? `${min}\u2013${max}` : ''

  return (
    <div style={{
      display: 'flex', justifyContent: 'space-between', alignItems: 'center',
      padding: '3px 0', fontSize: 12, fontFamily: 'monospace',
    }}>
      <span style={{ color: '#64748b' }}>
        {label}
        <span style={{
          fontSize: 9, fontWeight: 700, color: '#f59e0b', marginLeft: 6,
          letterSpacing: 1,
        }}>FORCE</span>
      </span>
      <span
        onClick={(e) => {
          setError(false)
          setOpen(true)
        }}
        style={{
          ...inputStyle, borderColor: '#f59e0b44',
          cursor: 'pointer', display: 'inline-flex', alignItems: 'center',
        }}
      >
        {displayVal}
        {unit && <span style={{ color: '#64748b', marginLeft: 4 }}>{unit}</span>}
      </span>
      {open && (
        <NumpadPopup
          label={label}
          hint={hint}
          value={displayVal}
          allowDecimal={!isXpdr && !isObs && decimals > 0}
          allowedDigits={isXpdr ? '01234567' : '0123456789'}
          autoDecimalAfter={(!isXpdr && !isObs && decimals > 0) ? 3 : 0}
          error={error}
          onSubmit={handleSubmit}
          onCancel={() => setOpen(false)}
        />
      )}
    </div>
  )
}

function voltageColor(v) {
  if (v > 13) return '#00ff88'
  if (v > 11) return '#f59e0b'
  return '#ef4444'
}

// 3-position selector: AUTO / ON / OFF
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

// ── Radio type registry ─────────────────────────────────────────────────────
// Maps radio type from YAML config → { unit, min, max, step, storeKey, wireKey }
// storeKey = zustand avionics field, wireKey = snake_case sent to backend

const RADIO_TYPES = {
  com:  { unit: 'MHz', min: 118.0,  max: 136.975, step: 0.025 },
  nav:  { unit: 'MHz', min: 108.0,  max: 117.95,  step: 0.05  },
  adf:  { unit: 'kHz', min: 190,    max: 1750,    step: 1     },
  obs:  { unit: '\u00B0', min: 0,   max: 360,     step: 1     },
  xpdr: { unit: '',    min: 0,      max: 7777,    step: 1     },
}

// Maps config radio id → { storeKey (camelCase in zustand), wireKey (snake_case for backend) }
const RADIO_FIELD_MAP = {
  com1: { storeKey: 'com1Mhz', wireKey: 'com1_mhz' },
  com2: { storeKey: 'com2Mhz', wireKey: 'com2_mhz' },
  com3: { storeKey: 'com3Mhz', wireKey: 'com3_mhz' },
  nav1: { storeKey: 'nav1Mhz', wireKey: 'nav1_mhz' },
  nav2: { storeKey: 'nav2Mhz', wireKey: 'nav2_mhz' },
  adf:  { storeKey: 'adf1Khz', wireKey: 'adf1_khz' },
  adf2: { storeKey: 'adf2Khz', wireKey: 'adf2_khz' },
  obs1: { storeKey: 'obs1Deg', wireKey: 'obs1_deg' },
  obs2: { storeKey: 'obs2Deg', wireKey: 'obs2_deg' },
  xpdr: { storeKey: 'xpdrCode', wireKey: 'xpdr_code' },
}

// ── Nav display components (per type) ───────────────────────────────────────

function GpsDisplay({ nav, id }) {
  const is2 = id === 'gps2'
  const valid = is2 ? nav.gps2Valid : nav.gps1Valid
  const lat = is2 ? nav.gps2LatDeg : nav.gps1LatDeg
  const lon = is2 ? nav.gps2LonDeg : nav.gps1LonDeg
  const alt = is2 ? nav.gps2AltFt : nav.gps1AltFt
  const gs = is2 ? nav.gps2GsKt : nav.gps1GsKt
  const trk = is2 ? nav.gps2TrackDeg : nav.gps1TrackDeg
  const label = is2 ? 'GPS2' : 'GPS1'
  return (
    <>
      <SectionHeader title={label} />
      {valid ? (
        <>
          <PanelRow label="LAT" value={lat?.toFixed(5) ?? '--'} unit={'\u00B0'} />
          <PanelRow label="LON" value={lon?.toFixed(5) ?? '--'} unit={'\u00B0'} />
          <PanelRow label="TRK" value={trk?.toFixed(0) ?? '--'} unit={'\u00B0'} />
          <PanelRow label="GS" value={gs?.toFixed(0) ?? '--'} unit="kt" />
          <PanelRow label="ALT" value={alt?.toFixed(0) ?? '--'} unit="ft" />
        </>
      ) : (
        <NoSignal text={`NO ${label}`} />
      )}
    </>
  )
}

function NavReceiverDisplay({ nav, avionics, id }) {
  const is2 = id === 'nav2'
  const valid = is2 ? nav.nav2Valid : nav.nav1Valid
  const ident = is2 ? nav.nav2Ident : nav.nav1Ident
  const type = is2 ? nav.nav2Type : nav.nav1Type
  const obsDeg = is2 ? nav.nav2ObsDeg : nav.nav1ObsDeg
  const cdiDots = is2 ? nav.nav2CdiDots : nav.nav1CdiDots
  const toFrom = is2 ? nav.nav2ToFrom : nav.nav1ToFrom
  const distanceNm = is2 ? nav.nav2DistanceNm : nav.nav1DistanceNm
  const gsValid = is2 ? nav.nav2GsValid : nav.nav1GsValid
  const gsDots = is2 ? nav.nav2GsDots : nav.nav1GsDots
  const freqMhz = is2 ? avionics.nav2Mhz : avionics.nav1Mhz
  const label = id.toUpperCase()
  const toFromColors = { TO: '#00ff88', FROM: '#f0e040', OFF: '#64748b' }
  return (
    <>
      <SectionHeader title={label} />
      <PanelRow label="Freq" value={freqMhz?.toFixed(2) ?? '--'} unit="MHz" />
      <PanelRow label="Type" value={valid ? `${type}${ident ? ' ' + ident : ''}` : '--'} />
      <PanelRow label="OBS" value={obsDeg?.toFixed(0) ?? '--'} unit={'\u00B0'} />
      <DeviationBar dots={cdiDots} valid={valid} label="CDI" />
      <div style={{
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
        padding: '4px 0', fontSize: 12, fontFamily: 'monospace',
      }}>
        <span style={{ color: '#64748b' }}>
          TO/FROM
          <NavFlag text={toFrom} active={valid} color={toFromColors[toFrom] || '#64748b'} />
        </span>
        <span style={{ color: '#e2e8f0' }}>
          {valid && distanceNm > 0 ? `${distanceNm.toFixed(1)} NM` : '--'}
        </span>
      </div>
      {(gsValid || type === 'ILS') && (
        <DeviationBar dots={gsDots} valid={gsValid} label="G/S" color="#c084fc" />
      )}
    </>
  )
}

function AdfDisplay({ nav, avionics, id }) {
  const is2 = id === 'adf2'
  const valid = is2 ? nav.adf2Valid : nav.adf1Valid
  const ident = is2 ? nav.adf2Ident : nav.adf1Ident
  const relBrg = is2 ? nav.adf2RelBearingDeg : nav.adf1RelBearingDeg
  const freq = is2 ? avionics.adf2Khz : avionics.adf1Khz
  const label = is2 ? 'ADF2' : 'ADF1'
  return (
    <>
      <SectionHeader title={label} />
      {valid ? (
        <>
          <PanelRow label="Freq" value={freq?.toFixed(0) ?? '--'} unit="kHz" />
          <PanelRow label="IDENT" value={ident || '--'} />
          <PanelRow label="REL BRG" value={relBrg?.toFixed(0) ?? '--'} unit={'\u00B0'} />
        </>
      ) : (
        <NoSignal />
      )}
    </>
  )
}

function DmeDisplay({ nav }) {
  return (
    <>
      <SectionHeader title="DME" />
      <PanelRow label="Source" value={nav.dmeSource} />
      {nav.dmeValid ? (
        <>
          <PanelRow label="DIST" value={nav.dmeDistanceNm?.toFixed(1) ?? '--'} unit="NM" />
          <PanelRow label="GS" value={nav.dmeGsKt?.toFixed(0) ?? '--'} unit="kt" />
        </>
      ) : (
        <NoSignal text={nav.dmeSource === 'HOLD' ? 'HOLD — NO DATA' : 'NO SIGNAL'} />
      )}
    </>
  )
}

function MarkersDisplay({ nav }) {
  return (
    <div style={{
      display: 'flex', gap: 8, padding: '8px 0', justifyContent: 'center',
    }}>
      <NavFlag text="OM" active={nav.markerOuter} color="#2563eb" />
      <NavFlag text="MM" active={nav.markerMiddle} color="#f59e0b" />
      <NavFlag text="IM" active={nav.markerInner} color="#e2e8f0" />
    </div>
  )
}

function XpdrDisplay({ nav }) {
  return (
    <>
      <SectionHeader title="TRANSPONDER" />
      <PanelRow label="Code" value={nav.xpdrCode?.toString().padStart(4, '0') ?? '--'} />
      <PanelRow label="Mode" value={nav.xpdrMode} />
    </>
  )
}

function TacanDisplay({ nav }) {
  return (
    <>
      <SectionHeader title="TACAN" />
      {nav.tacanValid ? (
        <>
          <PanelRow label="IDENT" value={nav.tacanIdent || '--'} />
          <PanelRow label="BRG" value={nav.tacanBearingDeg?.toFixed(0) ?? '--'} unit={'\u00B0'} />
          <PanelRow label="DIST" value={nav.tacanDistanceNm?.toFixed(1) ?? '--'} unit="NM" />
        </>
      ) : (
        <NoSignal />
      )}
    </>
  )
}

// Display component lookup by type
const DISPLAY_COMPONENTS = {
  gps:     GpsDisplay,
  nav:     NavReceiverDisplay,
  adf:     AdfDisplay,
  dme:     DmeDisplay,
  markers: MarkersDisplay,
  xpdr:    XpdrDisplay,
  tacan:   TacanDisplay,
}

// ── Main panel ──────────────────────────────────────────────────────────────

// Ground service selector IDs, keyed by the source type they control
const GROUND_SERVICE_SOURCES = {
  external_power: { selectorId: 'ext_pwr_cart', label: 'EXT PWR CART' },
  apu_generator:  { selectorId: 'apu_running',  label: 'APU' },
}

// ── Warning indicator ────────────────────────────────────────────────────────

function WarningFlag({ active, label }) {
  return active ? (
    <span style={{
      fontSize: 10, fontWeight: 700, fontFamily: 'monospace',
      padding: '1px 5px', borderRadius: 2, marginLeft: 6,
      background: '#ff3b30', color: '#fff', letterSpacing: 0.5,
    }}>{label}</span>
  ) : null
}

// ── Engine gauge row ─────────────────────────────────────────────────────────

function GaugeRow({ label, value, unit, warn, max }) {
  const pct = max > 0 ? Math.min(value / max, 1) : 0
  const barColor = warn ? '#ff3b30' : pct > 0.9 ? '#f59e0b' : '#00ff88'
  return (
    <div style={{ marginBottom: 4 }}>
      <div style={{
        display: 'flex', justifyContent: 'space-between', fontSize: 12,
        fontFamily: 'monospace', marginBottom: 2,
      }}>
        <span style={{ color: '#64748b' }}>{label}</span>
        <span style={{ color: warn ? '#ff3b30' : '#e2e8f0' }}>
          {typeof value === 'number' ? value.toFixed(1) : '--'} {unit}
        </span>
      </div>
      {max > 0 && (
        <div style={{ height: 6, background: '#1c2333', borderRadius: 2, overflow: 'hidden' }}>
          <div style={{
            height: '100%', borderRadius: 2, width: `${pct * 100}%`,
            background: barColor, transition: 'width 0.15s',
          }} />
        </div>
      )}
    </div>
  )
}

// ── Piston engine display ────────────────────────────────────────────────────

function PistonEngineDisplay({ idx, engines, config, fuel }) {
  const running = engines.engineRunning[idx]
  const cfg = config?.engines?.[idx] || {}
  const ffLph = fuel.engineFuelFlowLph?.[idx] ?? 0
  return (
    <>
      <div style={{
        display: 'flex', alignItems: 'center', gap: 8, marginBottom: 6,
        fontSize: 12, fontFamily: 'monospace',
      }}>
        <span style={{
          width: 8, height: 8, borderRadius: '50%',
          background: running ? '#00ff88' : '#ef4444',
        }} />
        <span style={{ color: '#e2e8f0', fontWeight: 700 }}>
          ENG {idx + 1} {running ? 'RUNNING' : 'OFF'}
        </span>
        {engines.engineFailed[idx] && <WarningFlag active label="FAIL" />}
        {engines.starterEngaged[idx] && (
          <span style={{ fontSize: 10, color: '#39d0d8', fontFamily: 'monospace' }}>STR</span>
        )}
      </div>
      <GaugeRow label="RPM" value={engines.rpm[idx]} unit=""
        max={cfg.rpm_max || 2700} warn={false} />
      <GaugeRow label="MAP" value={engines.manifoldPressureInhg[idx]} unit="inHg"
        max={cfg.manifold_pressure_max_inhg || 30} warn={false} />
      <GaugeRow label="EGT" value={engines.egtDegc[idx]} unit={'\u00B0C'}
        max={cfg.egt_max_degc || 900} warn={engines.highEgtWarning[idx]} />
      <GaugeRow label="CHT" value={engines.chtDegc[idx]} unit={'\u00B0C'}
        max={cfg.cht_max_degc || 240} warn={engines.highChtWarning[idx]} />
      <GaugeRow label="Oil P" value={engines.oilPressurePsi[idx]} unit="psi"
        max={cfg.oil_pressure_normal_psi || 80}
        warn={engines.lowOilPressureWarning[idx]} />
      <GaugeRow label="Oil T" value={engines.oilTempDegc[idx]} unit={'\u00B0C'}
        max={cfg.oil_temp_max_degc || 120} warn={false} />
      <GaugeRow label="FF" value={ffLph} unit="L/h"
        max={45} warn={false} />
    </>
  )
}

// ── Turboshaft engine display ────────────────────────────────────────────────

function TurboshaftEngineDisplay({ idx, engines, config, fuel }) {
  const running = engines.engineRunning[idx]
  const cfg = config?.engines?.[idx] || {}
  const ffLph = fuel.engineFuelFlowLph?.[idx] ?? 0
  return (
    <>
      <div style={{
        display: 'flex', alignItems: 'center', gap: 8, marginBottom: 6,
        fontSize: 12, fontFamily: 'monospace',
      }}>
        <span style={{
          width: 8, height: 8, borderRadius: '50%',
          background: running ? '#00ff88' : '#ef4444',
        }} />
        <span style={{ color: '#e2e8f0', fontWeight: 700 }}>
          ENG {idx + 1} {running ? 'RUNNING' : 'OFF'}
        </span>
        {engines.engineFailed[idx] && <WarningFlag active label="FAIL" />}
        {engines.starterEngaged[idx] && (
          <span style={{ fontSize: 10, color: '#39d0d8', fontFamily: 'monospace' }}>STR</span>
        )}
      </div>
      <GaugeRow label="N1" value={engines.n1Pct[idx]} unit="%"
        max={cfg.n1_max_pct || 100} warn={false} />
      <GaugeRow label="N2" value={engines.n2Pct[idx]} unit="%"
        max={cfg.n2_max_pct || 100} warn={false} />
      <GaugeRow label="TGT" value={engines.tgtDegc[idx]} unit={'\u00B0C'}
        max={cfg.tgt_max_degc || 810} warn={engines.highEgtWarning[idx]} />
      <GaugeRow label="TRQ" value={engines.torquePct[idx]} unit="%"
        max={cfg.torque_max_pct || 100} warn={false} />
      <GaugeRow label="Oil P" value={engines.oilPressurePsi[idx]} unit="psi"
        max={80} warn={engines.lowOilPressureWarning[idx]} />
      <GaugeRow label="Oil T" value={engines.oilTempDegc[idx]} unit={'\u00B0C'}
        max={120} warn={false} />
      <GaugeRow label="FF" value={ffLph} unit="L/h"
        max={180} warn={false} />
    </>
  )
}

export default function AircraftPanel() {
  const { fuel, aircraftId, nav, avionics, sendAvionics, electrical, sendPanel,
          avionicsConfig, electricalConfig, forcedSwitchIds,
          engines, engineConfig, fuelConfig } = useSimStore()

  // Track local selector values (not yet in electrical state feedback)
  const [groundSelectors, setGroundSelectors] = useState({})
  const [showLoads, setShowLoads] = useState(false)
  const [showCBs, setShowCBs] = useState(false)
  const [localForced, setLocalForced] = useState({})

  const { radios, displays } = avionicsConfig

  // Build the wire payload from current avionics state with one field updated
  const tune = useCallback((storeKey, val) => {
    const updated = { ...avionics, [storeKey]: val }
    // Build wire payload from all known field mappings
    const wire = {}
    for (const [, mapping] of Object.entries(RADIO_FIELD_MAP)) {
      if (updated[mapping.storeKey] !== undefined) {
        wire[mapping.wireKey] = updated[mapping.storeKey]
      }
    }
    wire.xpdr_mode = 0
    sendAvionics(wire)
  }, [avionics, sendAvionics])

  return (
    <div>
      {/* ── FUEL ──────────────────────────────────────── */}
      <SectionHeader title={`FUEL — ${fuelConfig.fuelType}`} />
      {fuel.tanks.length === 0 && (
        <div style={{ color: '#64748b', fontSize: 12 }}>No fuel data</div>
      )}
      {fuel.tanks.map((tank, i) => {
        const cfgTank = fuelConfig.tanks?.[i] || {}
        const name = cfgTank.name || `Tank ${i + 1}`
        const unit = fuelConfig.displayUnit || 'L'
        const displayQty = unit === 'L' ? tank.liters : tank.massKg
        const displayCap = unit === 'L' ? (cfgTank.capacity_liters || 0) : (cfgTank.capacity_kg || 0)
        return (
          <div key={i} style={{ marginBottom: 12 }}>
            <div style={{
              display: 'flex', justifyContent: 'space-between', fontSize: 12,
              fontFamily: 'monospace', marginBottom: 4,
            }}>
              <span style={{ color: '#64748b' }}>{name}</span>
              <span style={{ color: '#e2e8f0' }}>
                {displayQty?.toFixed(1) ?? '--'} {unit}  {((tank.pct ?? 0) * 100).toFixed(0)}%
              </span>
            </div>
            <div style={{ height: 10, background: '#1c2333', borderRadius: 2, overflow: 'hidden' }}>
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
      {(() => {
        const unit = fuelConfig.displayUnit || 'L'
        const totalDisplay = unit === 'L' ? fuel.totalFuelLiters : fuel.totalFuelKg
        return <PanelRow label="Total Fuel" value={`${totalDisplay?.toFixed(1) ?? '--'}`} unit={unit} />
      })()}
      <PanelRow label="Aircraft" value={aircraftId.toUpperCase()} />

      {/* ── ENGINES (dynamic from engine.yaml config) ─────── */}
      {engines.engineCount > 0 && (
        <>
          <SectionHeader title="ENGINES" />
          {Array.from({ length: engines.engineCount }).map((_, i) => {
            const type = engineConfig.engines?.[i]?.type || 'piston'
            return (
              <div key={i} style={{ marginBottom: i < engines.engineCount - 1 ? 12 : 0 }}>
                {type === 'turboshaft' ? (
                  <TurboshaftEngineDisplay idx={i} engines={engines} config={engineConfig} fuel={fuel} />
                ) : (
                  <PistonEngineDisplay idx={i} engines={engines} config={engineConfig} fuel={fuel} />
                )}
              </div>
            )
          })}
        </>
      )}

      {/* ── GROUND SERVICES (dynamic from source names) ── */}
      {(() => {
        const services = []
        electrical.sourceNames?.forEach((name) => {
          const upper = name.toUpperCase()
          if (upper.includes('EXT'))
            services.push({ selectorId: 'ext_pwr_cart', label: 'EXT PWR CART', sourceName: name })
          else if (upper.includes('APU'))
            services.push({ selectorId: 'apu_running', label: 'APU', sourceName: name })
        })
        if (services.length === 0) return null
        return (
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
        )
      })()}

      {/* ── ELECTRICAL (config-driven from electrical.yaml) ─── */}
      <SectionHeader title="ELECTRICAL" />
      {(() => {
        const cfg = electricalConfig
        if (!cfg) return (
          <div style={{ color: '#64748b', fontSize: 12, fontFamily: 'monospace' }}>
            Waiting for electrical config...
          </div>
        )

        // Look up live switch state by ID from ElectricalState
        const swClosed = (id) => {
          const idx = electrical.switchIds?.indexOf(id)
          return idx >= 0 ? (electrical.switchClosed[idx] ?? false) : false
        }
        const isForced = (id) => !!localForced[id]

        return (
          <>
            {/* Switches (pilot_controllable only) */}
            {cfg.switches?.filter(s => s.pilot_controllable !== false).map(sw => {
              const on = swClosed(sw.id)
              const forced = isForced(sw.id)
              return (
                <div key={sw.id} style={{
                  display: 'grid', gridTemplateColumns: '24px 1fr 48px',
                  alignItems: 'center', gap: 8,
                  padding: '4px 0', fontSize: 12, fontFamily: 'monospace',
                }}>
                  {/* Col 1: FRC checkbox — local state, no round-trip needed */}
                  <input
                    type="checkbox"
                    checked={forced}
                    onClick={(e) => {
                      e.stopPropagation()
                      const newForced = !forced
                      setLocalForced(prev => ({ ...prev, [sw.id]: newForced }))
                      sendPanel([sw.id], [on], null, null, [newForced])
                    }}
                    onChange={() => {}}
                    title={forced ? 'Release force' : 'Force switch'}
                    style={{
                      width: 14, height: 14, cursor: 'pointer',
                      accentColor: '#f59e0b',
                    }}
                  />
                  {/* Col 2: Label */}
                  <span style={{ color: '#94a3b8', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                    {sw.label}
                    {forced && (
                      <span style={{
                        fontSize: 8, fontWeight: 700, color: '#f59e0b', marginLeft: 6,
                        letterSpacing: 1, verticalAlign: 'super',
                      }}>FORCED</span>
                    )}
                  </span>
                  {/* Col 3: Toggle — forced: change forced value; unforced: normal command */}
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
            })}

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

            {/* Circuit Breakers (collapsible) */}
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
                {showCBs && electrical.cbNames.map((name, i) => (
                  <div key={name} style={{
                    display: 'flex', justifyContent: 'space-between', alignItems: 'center',
                    padding: '1px 0 1px 12px', fontSize: 11, fontFamily: 'monospace',
                  }}>
                    <span style={{ color: electrical.cbTripped[i] ? '#ff3b30' : '#94a3b8' }}>{name}</span>
                    <span style={{
                      color: electrical.cbTripped[i] ? '#ff3b30' : electrical.cbClosed[i] ? '#00ff88' : '#475569',
                    }}>
                      {electrical.cbTripped[i] ? 'TRIPPED' : electrical.cbClosed[i] ? 'OK' : 'OPEN'}
                    </span>
                  </div>
                ))}
              </>
            )}
          </>
        )
      })()}

      {/* ── NAV DISPLAYS (dynamic from config) ─────────── */}
      {displays.map(({ id, type }) => {
        const Comp = DISPLAY_COMPONENTS[type]
        if (!Comp) return null
        return <Comp key={id} nav={nav} avionics={avionics} id={id} />
      })}

      {/* ── RADIO TUNING (dynamic from config) ─────────── */}
      {radios.length > 0 && <SectionHeader title="RADIO TUNING ▸ FORCE" />}
      {radios.map(({ id, label, type }) => {
        const typeInfo = RADIO_TYPES[type]
        const fieldInfo = RADIO_FIELD_MAP[id]
        if (!typeInfo || !fieldInfo) return null
        const value = avionics[fieldInfo.storeKey] ?? 0
        return (
          <FreqInput key={id} label={label}
            value={value}
            unit={typeInfo.unit}
            step={typeInfo.step}
            min={typeInfo.min}
            max={typeInfo.max}
            radioType={type}
            onChange={v => tune(fieldInfo.storeKey, v)}
          />
        )
      })}
    </div>
  )
}
