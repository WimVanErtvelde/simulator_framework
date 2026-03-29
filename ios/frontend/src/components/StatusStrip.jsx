import { useState, useCallback } from 'react'
import { useSimStore } from '../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import NumpadPopup from './ui/NumpadPopup'

const STATE_BADGES = {
  RUNNING:       { bg: '#1a4731', text: '#3fb950', border: '#2d6a4f' },
  FROZEN:        { bg: '#1a2744', text: '#388bfd', border: '#2d4a8f' },
  READY:         { bg: '#3d2b0a', text: '#d29922', border: '#7d5a14' },
  INIT:          { bg: '#1c1c1c', text: '#8b949e', border: '#30363d' },
  REPOSITIONING: { bg: '#3b1f6e', text: '#c4b5fd', border: '#6d3fc0' },
}
const DEFAULT_BADGE = { bg: '#3d0a0a', text: '#f85149', border: '#8f2d2d' }

const XPDR_BADGES = {
  ALT:  { bg: '#1a4731', text: '#3fb950', border: '#2d6a4f' },
  STBY: { bg: '#1c1c1c', text: '#8b949e', border: '#30363d' },
  GND:  { bg: '#1a2744', text: '#388bfd', border: '#2d4a8f' },
  ON:   { bg: '#3d2b0a', text: '#d29922', border: '#7d5a14' },
  EMER: { bg: '#3d0a0a', text: '#f85149', border: '#8f2d2d' },
}

function fmtTime(s) {
  const h = Math.floor(s / 3600).toString().padStart(2, '0')
  const m = Math.floor((s % 3600) / 60).toString().padStart(2, '0')
  const sec = Math.floor(s % 60).toString().padStart(2, '0')
  return `${h}:${m}:${sec}`
}

function Badge({ label, colors }) {
  return (
    <span style={{
      padding: '4px 12px', borderRadius: 8, fontWeight: 700, fontSize: 13,
      textTransform: 'uppercase', background: colors.bg, color: colors.text,
      border: `1px solid ${colors.border}`,
    }}>{label}</span>
  )
}

function Sep() {
  return <span style={{ color: '#30363d', margin: '0 8px' }}>|</span>
}

const rowStyle = {
  display: 'flex', alignItems: 'center', minHeight: 32,
  padding: '0 14px', fontSize: 14, fontFamily: "'JetBrains Mono', 'Fira Code', 'SF Mono', monospace",
}

// Maps radio id → { storeKey, decimals } for header display
const RADIO_HEADER = {
  com1: { storeKey: 'com1Mhz', decimals: 2 },
  com2: { storeKey: 'com2Mhz', decimals: 2 },
  com3: { storeKey: 'com3Mhz', decimals: 2 },
  nav1: { storeKey: 'nav1Mhz', decimals: 2 },
  nav2: { storeKey: 'nav2Mhz', decimals: 2 },
  adf:  { storeKey: 'adf1Khz', decimals: 0 },
  adf2: { storeKey: 'adf2Khz', decimals: 0 },
}

const TERRAIN_INDICATORS = {
  CIGI:    { dot: '#3fb950', label: 'CIGI HOT' },
  SRTM:    { dot: '#d29922', label: 'SRTM' },
  MSL:     { dot: '#f85149', label: 'MSL' },
  UNKNOWN: { dot: '#8b949e', label: 'NO TERR' },
}

// Radio config for numpad popups (type → validation + display)
const RADIO_TUNING = {
  com1: { storeKey: 'com1Mhz', wireKey: 'com1_mhz', type: 'com' },
  com2: { storeKey: 'com2Mhz', wireKey: 'com2_mhz', type: 'com' },
  com3: { storeKey: 'com3Mhz', wireKey: 'com3_mhz', type: 'com' },
  nav1: { storeKey: 'nav1Mhz', wireKey: 'nav1_mhz', type: 'nav' },
  nav2: { storeKey: 'nav2Mhz', wireKey: 'nav2_mhz', type: 'nav' },
  adf:  { storeKey: 'adf1Khz', wireKey: 'adf1_khz', type: 'adf' },
  adf2: { storeKey: 'adf2Khz', wireKey: 'adf2_khz', type: 'adf' },
}

const RADIO_VALIDATE = {
  com: { min: 118.0, max: 136.975, decimal: true, digits: '0123456789', hint: '118.00\u2013136.97' },
  nav: { min: 108.0, max: 117.95, decimal: true, digits: '0123456789', hint: '108.00\u2013117.95' },
  adf: { min: 190, max: 1750, decimal: false, digits: '0123456789', hint: '190\u20131750 kHz' },
}

// Build a full avionics wire payload from current state with one field updated
const WIRE_KEYS = {
  com1Mhz: 'com1_mhz', com2Mhz: 'com2_mhz', com3Mhz: 'com3_mhz',
  nav1Mhz: 'nav1_mhz', nav2Mhz: 'nav2_mhz',
  adf1Khz: 'adf1_khz', adf2Khz: 'adf2_khz',
  xpdrCode: 'xpdr_code', obs1Deg: 'obs1_deg', obs2Deg: 'obs2_deg',
}

function buildAvionicsPayload(avionics, storeKey, newVal) {
  const updated = { ...avionics, [storeKey]: newVal }
  const wire = {}
  for (const [sk, wk] of Object.entries(WIRE_KEYS)) {
    if (updated[sk] !== undefined) wire[wk] = updated[sk]
  }
  wire.xpdr_mode = 0
  return wire
}

export default function StatusStrip() {
  const { simState, simTimeSec, aircraftId, fdm, nav, atmosphere,
          armedFailures, activeFailures, avionics, avionicsConfig,
          terrainSource, sendAvionics } = useSimStore(useShallow(s => ({
    simState: s.simState, simTimeSec: s.simTimeSec, aircraftId: s.aircraftId,
    fdm: s.fdm, nav: s.nav, atmosphere: s.atmosphere,
    armedFailures: s.armedFailures, activeFailures: s.activeFailures,
    avionics: s.avionics, avionicsConfig: s.avionicsConfig,
    terrainSource: s.terrainSource, sendAvionics: s.sendAvionics,
  })))
  const [numpad, setNumpad] = useState(null)   // { id, label, anchor }
  const [numpadError, setNumpadError] = useState(false)
  const dim = simState === 'UNKNOWN' || simState === 'INIT'
  const v = (val, decimals = 0) => {
    if (dim) return '--'
    if (typeof val !== 'number') return val
    const rounded = +val.toFixed(decimals)
    return (Object.is(rounded, -0) ? 0 : rounded).toFixed(decimals)
  }
  const badge = STATE_BADGES[simState] ?? DEFAULT_BADGE

  const openNumpad = useCallback((id, label, e) => {
    const rect = e.currentTarget.getBoundingClientRect()
    setNumpadError(false)
    setNumpad({ id, label, anchor: { x: rect.left, y: rect.bottom } })
  }, [])

  const handleNumpadSubmit = useCallback((val) => {
    if (!numpad) return
    const tuning = RADIO_TUNING[numpad.id]
    const isXpdr = numpad.id === 'xpdr'

    if (isXpdr) {
      // Validate XPDR: 4 digits, octal (0-7 each)
      if (!/^[0-7]{4}$/.test(val)) {
        setNumpadError(true)
        return
      }
      const wire = buildAvionicsPayload(avionics, 'xpdrCode', parseInt(val, 10))
      sendAvionics(wire)
    } else if (tuning) {
      const rv = RADIO_VALIDATE[tuning.type]
      const num = parseFloat(val)
      if (isNaN(num) || num < rv.min || num > rv.max) {
        setNumpadError(true)
        return
      }
      const wire = buildAvionicsPayload(avionics, tuning.storeKey, num)
      sendAvionics(wire)
    }
    setNumpad(null)
  }, [numpad, avionics, sendAvionics])

  let xpdrBadge = XPDR_BADGES[avionics.xpdrMode] ?? XPDR_BADGES.STBY
  if (String(avionics.xpdrCode) === '7700') xpdrBadge = XPDR_BADGES.EMER

  return (
    <div style={{ background: '#0d1117', borderBottom: '1px solid #1e293b' }}>
      {/* Row 1 — Sim state + flight params */}
      <div style={rowStyle}>
        <Badge label={simState} colors={badge} />
        <Sep /><span style={{ color: '#64748b' }}>SIM</span>&nbsp;{dim ? '--:--:--' : fmtTime(simTimeSec)}
        <Sep />{aircraftId.toUpperCase()}
        <Sep /><span style={{ color: '#39d0d8', fontSize: '0.7em', fontWeight: 600 }}>TRUTH</span>
        &nbsp;{v(fdm.altFtMsl)}ft
        <Sep /><span style={{ color: '#64748b' }}>IAS</span>&nbsp;{v(fdm.iasKt)}kt
        <Sep /><span style={{ color: '#64748b' }}>VS</span>&nbsp;{v(fdm.vsFpm)}fpm
        <Sep /><span style={{ color: '#64748b' }}>HDG</span>&nbsp;{v(nav.hdgMagDeg)}°
        <Sep /><span style={{ color: '#64748b' }}>GS</span>&nbsp;{v(fdm.gndSpeedKt)}kt
      </div>
      {/* Row 2 — Environment */}
      <div style={rowStyle}>
        <span style={{ color: '#64748b' }}>QNH</span>&nbsp;{v(atmosphere.qnhHpa, 1)}hPa
        <Sep /><span style={{ color: '#64748b' }}>WIND</span>&nbsp;{v(atmosphere.windDirDeg)}°/{v(atmosphere.windSpeedKt)}kt
        <Sep /><span style={{ color: '#64748b' }}>OAT</span>&nbsp;{v(atmosphere.oatCelsius, 1)}°C
        <Sep /><span style={{ color: '#64748b' }}>VIS</span>&nbsp;{v(atmosphere.visibilityM)}m
        <Sep /><span style={{ color: '#64748b' }}>armed:</span>{v(armedFailures)}
        &nbsp;<span style={{ color: activeFailures > 0 ? '#f85149' : '#64748b' }}>active:{v(activeFailures)}</span>
        <Sep />
        {(() => {
          const ti = TERRAIN_INDICATORS[terrainSource.source] ?? TERRAIN_INDICATORS.UNKNOWN
          return (
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }}>
              <span style={{
                width: 8, height: 8, borderRadius: '50%', background: ti.dot,
                display: 'inline-block', boxShadow: `0 0 4px ${ti.dot}`,
              }} />
              <span style={{ color: ti.dot }}>{ti.label}</span>
            </span>
          )
        })()}
      </div>
      {/* Row 3 — Radios (dynamic from aircraft config) */}
      <div style={rowStyle}>
        {avionicsConfig.radios.map(({ id, label, type }, i) => {
          if (type === 'obs' || type === 'xpdr') return null
          const info = RADIO_HEADER[id]
          if (!info) return null
          const tappable = !!RADIO_TUNING[id]
          return (
            <span key={id}>
              {i > 0 && <Sep />}
              <span
                style={{
                  cursor: tappable ? 'pointer' : 'default',
                  borderBottom: tappable ? '1px dashed #334155' : 'none',
                  padding: '2px 0',
                }}
                onClick={tappable ? (e) => openNumpad(id, label, e) : undefined}
              >
                <span style={{ color: '#64748b' }}>{label}</span>&nbsp;{v(avionics[info.storeKey], info.decimals)}
              </span>
            </span>
          )
        })}
        <Sep />
        <span
          style={{ cursor: 'pointer', borderBottom: '1px dashed #334155', padding: '2px 0' }}
          onClick={(e) => openNumpad('xpdr', 'XPDR', e)}
        >
          <span style={{ color: '#64748b' }}>XPDR</span>&nbsp;{v(avionics.xpdrCode)}
        </span>&nbsp;
        <Badge label={avionics.xpdrMode} colors={xpdrBadge} />
      </div>

      {/* Numpad popup for radio tuning */}
      {numpad && (() => {
        const tuning = RADIO_TUNING[numpad.id]
        const isXpdr = numpad.id === 'xpdr'
        const rv = tuning ? RADIO_VALIDATE[tuning.type] : null
        const currentVal = isXpdr
          ? String(avionics.xpdrCode).padStart(4, '0')
          : tuning ? Number(avionics[tuning.storeKey]).toFixed(rv?.decimal ? 2 : 0) : ''
        return (
          <NumpadPopup
            label={numpad.label}
            hint={isXpdr ? '0000\u20137777' : rv?.hint ?? ''}
            value={currentVal}
            allowDecimal={isXpdr ? false : rv?.decimal ?? true}
            allowedDigits={isXpdr ? '01234567' : rv?.digits ?? '0123456789'}
            autoDecimalAfter={rv?.decimal ? 3 : 0}
            anchor={numpad.anchor}
            error={numpadError}
            onSubmit={handleNumpadSubmit}
            onCancel={() => setNumpad(null)}
          />
        )
      })()}
    </div>
  )
}
