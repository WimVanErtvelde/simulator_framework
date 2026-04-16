import { useState, useCallback } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import NumpadPopup from '../../components/ui/NumpadPopup'

// Embeddable radio tuning section with power gating.
// Used by CockpitAvionics (standalone) and C172Panel (compact embed).
// Publishes VIRTUAL priority via sendVirtualAvionics.

// ── Radio constants (duplicated from RadiosTab — different priority tier) ───

const RADIO_TYPES = {
  com:  { unit: 'MHz', min: 118.0,  max: 136.975, step: 0.025, decimals: 3 },
  nav:  { unit: 'MHz', min: 108.0,  max: 117.95,  step: 0.05,  decimals: 2 },
  adf:  { unit: 'kHz', min: 190,    max: 1750,    step: 1,     decimals: 0 },
  obs:  { unit: '\u00B0', min: 0,   max: 360,     step: 1,     decimals: 0 },
  xpdr: { unit: '',    min: 0,      max: 7777,    step: 1,     decimals: 0 },
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

// Radio group order for section dividers
const GROUP_ORDER = ['com', 'nav', 'adf', 'obs', 'xpdr']

function radioGroup(type) {
  if (type === 'com') return 'com'
  if (type === 'nav') return 'nav'
  if (type === 'adf') return 'adf'
  if (type === 'obs') return 'obs'
  if (type === 'xpdr') return 'xpdr'
  return 'other'
}

// ── Power gating helpers ───────────────────────────────────────────────────

function isRadioPowered(radioId, loadNames, loadPowered) {
  // Find matching load by exact id — solver already models full topology
  // (bus power, CB state, switch state). No bus-level checks needed.
  if (!loadNames?.length) return true // graceful fallback
  const idx = loadNames.indexOf(radioId)
  if (idx >= 0) return loadPowered[idx] ?? false
  return true // no matching load found — assume powered
}

// ── Radio row component ────────────────────────────────────────────────────

function RadioRow({ label, value, typeInfo, powered, compact, onTune }) {
  const [open, setOpen] = useState(false)
  const [error, setError] = useState(false)
  const isXpdr = typeInfo === RADIO_TYPES.xpdr
  const isObs = typeInfo === RADIO_TYPES.obs

  const displayVal = isXpdr
    ? String(Math.round(value)).padStart(4, '0')
    : typeof value === 'number' ? value.toFixed(typeInfo.decimals) : String(value)

  const hint = isXpdr ? '0000\u20137777'
    : isObs ? '0\u2013360'
    : (typeInfo.min != null && typeInfo.max != null) ? `${typeInfo.min}\u2013${typeInfo.max}` : ''

  const handleSubmit = useCallback((val) => {
    if (isXpdr) {
      if (!/^[0-7]{4}$/.test(val)) { setError(true); return }
      onTune(parseInt(val, 10))
    } else {
      const num = parseFloat(val)
      if (isNaN(num) || num < typeInfo.min || num > typeInfo.max) {
        setError(true); return
      }
      onTune(num)
    }
    setOpen(false)
  }, [typeInfo, onTune, isXpdr])

  const rowH = compact ? 24 : 48

  if (!powered) {
    return (
      <div style={{
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
        minHeight: rowH, padding: compact ? '1px 0' : '4px 0',
        borderBottom: '1px solid #111827',
      }}>
        <span style={{ fontSize: compact ? 9 : 13, fontWeight: 700, color: '#475569',
          fontFamily: 'monospace', letterSpacing: 1 }}>{label}</span>
        <span style={{
          fontSize: compact ? 11 : 16, fontFamily: 'monospace', fontWeight: 700,
          color: '#1e293b', minWidth: compact ? 72 : 120, textAlign: 'right',
          padding: compact ? '2px 4px' : '4px 8px',
        }}>------</span>
      </div>
    )
  }

  return (
    <div style={{
      display: 'flex', justifyContent: 'space-between', alignItems: 'center',
      minHeight: rowH, padding: compact ? '1px 0' : '4px 0',
      borderBottom: '1px solid #111827',
    }}>
      <span style={{ fontSize: compact ? 9 : 13, fontWeight: 700, color: '#00ff88',
        fontFamily: 'monospace', letterSpacing: 1 }}>{label}</span>
      <span
        onClick={() => { setError(false); setOpen(true) }}
        style={{
          fontSize: compact ? 11 : 16, fontFamily: 'monospace', fontWeight: 700,
          color: '#e2e8f0', minWidth: compact ? 72 : 120, textAlign: 'right',
          padding: compact ? '2px 4px' : '4px 8px', cursor: 'pointer', borderRadius: 3,
          background: '#0d1117', border: '1px solid #1e293b',
          transition: 'border-color 0.15s',
        }}
      >
        {displayVal}
        {typeInfo.unit && (
          <span style={{ color: '#64748b', marginLeft: 4, fontSize: compact ? 8 : 12 }}>
            {typeInfo.unit}
          </span>
        )}
      </span>
      {open && (
        <NumpadPopup
          label={label}
          hint={hint}
          value={displayVal}
          allowDecimal={!isXpdr && !isObs && typeInfo.decimals > 0}
          allowedDigits={isXpdr ? '01234567' : '0123456789'}
          autoDecimalAfter={(!isXpdr && !isObs && typeInfo.decimals > 0) ? 3 : 0}
          error={error}
          onSubmit={handleSubmit}
          onCancel={() => setOpen(false)}
        />
      )}
    </div>
  )
}

// ── Main component ─────────────────────────────────────────────────────────

export default function RadioSection({ compact = false }) {
  const { avionics, electrical, avionicsConfig, sendVirtualAvionics } = useSimStore(useShallow(s => ({
    avionics: s.avionics,
    electrical: s.electrical,
    avionicsConfig: s.avionicsConfig,
    sendVirtualAvionics: s.sendVirtualAvionics,
  })))

  const { radios } = avionicsConfig
  const { loadNames, loadPowered } = electrical

  const tune = useCallback((storeKey, val) => {
    const updated = { ...avionics, [storeKey]: val }
    const wire = {}
    for (const [, mapping] of Object.entries(RADIO_FIELD_MAP)) {
      if (updated[mapping.storeKey] !== undefined) {
        wire[mapping.wireKey] = updated[mapping.storeKey]
      }
    }
    wire.xpdr_mode = 0
    sendVirtualAvionics(wire)
  }, [avionics, sendVirtualAvionics])

  if (!radios?.length) {
    return (
      <div style={{ color: '#64748b', fontSize: 12, fontFamily: 'monospace', padding: 8 }}>
        Waiting for avionics config...
      </div>
    )
  }

  // Group radios for section dividers
  // OBS is excluded — it's a gauge input (HSI/CDI knob), not a radio tuning field
  let lastGroup = null
  const rows = []
  for (const radio of radios) {
    if (radio.type === 'obs') continue
    const typeInfo = RADIO_TYPES[radio.type]
    const fieldInfo = RADIO_FIELD_MAP[radio.id]
    if (!typeInfo || !fieldInfo) continue

    const grp = radioGroup(radio.type)
    if (lastGroup && grp !== lastGroup) {
      rows.push(
        <div key={`div-${grp}`} style={{
          borderTop: '1px solid #1e293b', margin: compact ? '2px 0' : '8px 0',
        }} />
      )
    }
    lastGroup = grp

    const powered = isRadioPowered(radio.id, loadNames, loadPowered)
    const value = avionics[fieldInfo.storeKey] ?? 0

    rows.push(
      <RadioRow
        key={radio.id}
        label={radio.label}
        value={value}
        typeInfo={typeInfo}
        powered={powered}
        compact={compact}
        onTune={v => tune(fieldInfo.storeKey, v)}
      />
    )
  }

  return <div>{rows}</div>
}
