import { useSimStore } from '../store/useSimStore'

const STATE_BADGES = {
  RUNNING:  { bg: '#1a4731', text: '#3fb950', border: '#2d6a4f' },
  FROZEN:   { bg: '#1a2744', text: '#388bfd', border: '#2d4a8f' },
  READY:    { bg: '#3d2b0a', text: '#d29922', border: '#7d5a14' },
  INIT:     { bg: '#1c1c1c', text: '#8b949e', border: '#30363d' },
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

export default function StatusStrip() {
  const { simState, simTimeSec, aircraftId, fdm, atmosphere,
          armedFailures, activeFailures, avionics, avionicsConfig,
          terrainSource } = useSimStore()
  const dim = simState === 'UNKNOWN' || simState === 'INIT'
  const v = (val, decimals = 0) => {
    if (dim) return '--'
    if (typeof val !== 'number') return val
    const rounded = +val.toFixed(decimals)
    return (Object.is(rounded, -0) ? 0 : rounded).toFixed(decimals)
  }
  const badge = STATE_BADGES[simState] ?? DEFAULT_BADGE

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
        <Sep /><span style={{ color: '#64748b' }}>HDG</span>&nbsp;{v(fdm.hdgMagDeg)}°
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
          return (
            <span key={id}>
              {i > 0 && <Sep />}
              <span style={{ color: '#64748b' }}>{label}</span>&nbsp;{v(avionics[info.storeKey], info.decimals)}
            </span>
          )
        })}
        <Sep /><span style={{ color: '#64748b' }}>XPDR</span>&nbsp;{v(avionics.xpdrCode)}&nbsp;
        <Badge label={avionics.xpdrMode} colors={xpdrBadge} />
      </div>
    </div>
  )
}
