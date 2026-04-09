import { useState, useCallback } from 'react'
import { useSimStore } from '../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { PanelRow, SectionHeader } from '../PanelUtils'
import NumpadPopup from '../../ui/NumpadPopup'

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
        onClick={() => { setError(false); setOpen(true) }}
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

// ── Radio type registry ─────────────────────────────────────────────────────

const RADIO_TYPES = {
  com:  { unit: 'MHz', min: 118.0,  max: 136.975, step: 0.025 },
  nav:  { unit: 'MHz', min: 108.0,  max: 117.95,  step: 0.05  },
  adf:  { unit: 'kHz', min: 190,    max: 1750,    step: 1     },
  obs:  { unit: '\u00B0', min: 0,   max: 360,     step: 1     },
  xpdr: { unit: '',    min: 0,      max: 7777,    step: 1     },
}

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

// ── Nav display components ──────────────────────────────────────────────────

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
    <div style={{ display: 'flex', gap: 8, padding: '8px 0', justifyContent: 'center' }}>
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

const DISPLAY_COMPONENTS = {
  gps:     GpsDisplay,
  nav:     NavReceiverDisplay,
  adf:     AdfDisplay,
  dme:     DmeDisplay,
  markers: MarkersDisplay,
  xpdr:    XpdrDisplay,
  tacan:   TacanDisplay,
}

// ── Main radios tab ────────────────────────────────────────────────────────

export default function RadiosTab() {
  const { nav, avionics, sendAvionics, avionicsConfig } = useSimStore(useShallow(s => ({
    nav: s.nav, avionics: s.avionics, sendAvionics: s.sendAvionics,
    avionicsConfig: s.avionicsConfig,
  })))

  const { radios, displays } = avionicsConfig

  const tune = useCallback((storeKey, val) => {
    const updated = { ...avionics, [storeKey]: val }
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
      {/* Nav displays */}
      {displays.map(({ id, type }) => {
        const Comp = DISPLAY_COMPONENTS[type]
        if (!Comp) return null
        return <Comp key={id} nav={nav} avionics={avionics} id={id} />
      })}

      {/* Radio tuning */}
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
