import { useState, useEffect, useRef, useCallback } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { PanelRow, SectionHeader, FullWidthBtn } from './PanelUtils'
import KeyboardPopup from '../ui/KeyboardPopup'
import NumpadPopup from '../ui/NumpadPopup'

const DEG2RAD = Math.PI / 180
const NM_TO_DEG_LAT = 1 / 60
const FT2M = 0.3048
const KT2MS = 0.514444

const inputBase = {
  width: '100%', height: 44, padding: '8px 12px',
  background: '#1c2333', border: '1px solid #1e293b', borderRadius: 3,
  color: '#e2e8f0', fontFamily: 'monospace', fontSize: 13,
  WebkitAppearance: 'none', outline: 'none',
}

// ─── Aircraft icon SVG ───────────────────────────────────────────────────────
function AircraftIcon({ rotation = 0, size = 24, color = '#64748b' }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24"
      style={{ transform: `rotate(${rotation}deg)`, transition: 'transform 0.2s' }}>
      <path d="M12 2 L8 14 L4 14 L12 22 L20 14 L16 14 Z" fill={color} />
    </svg>
  )
}

// ─── Airport search with dropdown ────────────────────────────────────────────
function AirportSearch({ value, onSelect, placeholder = 'ICAO or name' }) {
  const { searchAirports, airportSearchResults } = useSimStore(useShallow(s => ({
    searchAirports: s.searchAirports, airportSearchResults: s.airportSearchResults,
  })))
  const [query, setQuery] = useState('')
  const [kbOpen, setKbOpen] = useState(false)
  const debounceRef = useRef(null)

  const doSearch = useCallback((v) => {
    setQuery(v)
    clearTimeout(debounceRef.current)
    if (v.length >= 2) {
      debounceRef.current = setTimeout(() => searchAirports(v), 200)
    }
  }, [searchAirports])

  const handleSelect = (apt) => {
    setQuery('')
    setKbOpen(false)
    onSelect(apt)
  }

  const displayValue = value
    ? `${value.icao} — ${value.name}`
    : query

  return (
    <div style={{ position: 'relative' }}>
      <div
        style={{
          ...inputBase, cursor: 'pointer',
          display: 'flex', alignItems: 'center',
          color: displayValue ? '#e2e8f0' : '#475569',
        }}
        onClick={(e) => {
          if (value) { onSelect(null); setQuery('') }
          setKbOpen(true)
        }}
      >
        {displayValue || placeholder}
      </div>
      {kbOpen && (
        <KeyboardPopup
          label="Airport search"
          value={query}
          onChange={(val) => {
            const v = val.toUpperCase()
            doSearch(v)
          }}
          onSubmit={(val) => {
            const v = val.toUpperCase()
            doSearch(v)
            setKbOpen(false)
          }}
          onCancel={() => setKbOpen(false)}
        >
          {airportSearchResults.length > 0 && (
            <div style={{
              maxHeight: 180, overflowY: 'auto', marginBottom: 8,
              border: '1px solid #1e293b', borderRadius: 3, background: '#111827',
            }}>
              {airportSearchResults.map((apt) => (
                <div
                  key={apt.icao}
                  onClick={() => handleSelect(apt)}
                  style={{
                    padding: '10px 12px', cursor: 'pointer', fontSize: 13,
                    fontFamily: 'monospace', color: '#e2e8f0',
                    borderBottom: '1px solid #1e293b',
                  }}
                  onMouseEnter={(e) => e.currentTarget.style.background = 'rgba(0,255,136,0.06)'}
                  onMouseLeave={(e) => e.currentTarget.style.background = 'transparent'}
                >
                  <span style={{ color: '#00ff88', fontWeight: 700 }}>{apt.icao}</span>
                  <span style={{ color: '#64748b' }}> — </span>
                  {apt.name}
                  {apt.elevation_m > 0 && (
                    <span style={{ color: '#64748b', fontSize: 11 }}>
                      {' '}{Math.round(apt.elevation_m / FT2M)}ft
                    </span>
                  )}
                </div>
              ))}
            </div>
          )}
        </KeyboardPopup>
      )}
    </div>
  )
}

// ─── Runway selector buttons ─────────────────────────────────────────────────
function RunwaySelector({ airport, selected, onSelect }) {
  if (!airport?.runways?.length) return null

  const ends = []
  for (const rwy of airport.runways) {
    if (rwy.designator_end1) {
      ends.push({
        designator: rwy.designator_end1,
        lat_deg: rwy.threshold_lat_deg_end1,
        lon_deg: rwy.threshold_lon_deg_end1,
        heading_deg: rwy.heading_deg_end1,
        displaced_m: rwy.displaced_threshold_m_end1,
        elevation_m: rwy.elevation_m || airport.elevation_m,
        length_m: rwy.length_m,
      })
    }
    if (rwy.designator_end2) {
      ends.push({
        designator: rwy.designator_end2,
        lat_deg: rwy.threshold_lat_deg_end2,
        lon_deg: rwy.threshold_lon_deg_end2,
        heading_deg: rwy.heading_deg_end2,
        displaced_m: rwy.displaced_threshold_m_end2,
        elevation_m: rwy.elevation_m || airport.elevation_m,
        length_m: rwy.length_m,
      })
    }
  }
  ends.sort((a, b) => a.designator.localeCompare(b.designator))

  return (
    <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 8 }}>
      {ends.map((e) => {
        const active = selected?.designator === e.designator
        return (
          <button key={e.designator} onClick={() => onSelect(e)}
            style={{
              padding: '6px 14px', borderRadius: 3, fontFamily: 'monospace',
              fontSize: 13, fontWeight: 700, cursor: 'pointer',
              border: `1px solid ${active ? '#00ff88' : '#1e293b'}`,
              background: active ? 'rgba(0,255,136,0.12)' : '#1c2333',
              color: active ? '#00ff88' : '#e2e8f0',
              touchAction: 'manipulation',
            }}
          >{e.designator}</button>
        )
      })}
    </div>
  )
}

// ─── Departure position icons ────────────────────────────────────────────────
const POSITIONS = [
  { id: 'rwy',   label: 'RWY',   altFt: 0,    distNm: 0, kt: 0,   bearing: 0,   lateral: 0 },
  { id: '2nm',   label: '2NM',   altFt: 600,  distNm: 2, kt: 80,  bearing: 180, lateral: 0 },
  { id: '4nm',   label: '4NM',   altFt: 1200, distNm: 4, kt: 90,  bearing: 180, lateral: 0 },
  { id: 'down',  label: 'DWN',   altFt: 1000, distNm: 0, kt: 100, bearing: 180, lateral: -1, abeam: true },
]

function computePosition(rwy, airport, p) {
  const hdg = rwy.heading_deg * DEG2RAD
  const cos_lat = Math.cos(rwy.lat_deg * DEG2RAD)
  const elev_m = rwy.elevation_m || airport.elevation_m || 0
  const alt_m = elev_m + p.altFt * FT2M

  let n_nm = 0, e_nm = 0
  let heading = rwy.heading_deg

  // For ground positions, offset past the displaced threshold + piano bars (~30m)
  if (p.altFt === 0) {
    const offset_m = (rwy.displaced_m || 0) + 30
    const offset_nm = offset_m / 1852
    n_nm += offset_nm * Math.cos(hdg)
    e_nm += offset_nm * Math.sin(hdg)
  }

  // Along centerline (behind threshold)
  if (p.distNm > 0) {
    const back = hdg + Math.PI
    n_nm += p.distNm * Math.cos(back)
    e_nm += p.distNm * Math.sin(back)
  }

  // Lateral offset
  if (p.lateral !== 0) {
    const side = hdg + (p.lateral > 0 ? Math.PI / 2 : -Math.PI / 2)
    n_nm += Math.cos(side)
    e_nm += Math.sin(side)

    if (p.abeam) {
      // Downwind: abeam midpoint, opposite heading
      const half = (rwy.length_m / 1852) / 2
      n_nm += half * Math.cos(hdg)
      e_nm += half * Math.sin(hdg)
      heading = (rwy.heading_deg + 180) % 360
    } else if (p.past) {
      // Crosswind: 1nm past threshold
      n_nm += Math.cos(hdg)
      e_nm += Math.sin(hdg)
      heading = (rwy.heading_deg + 90) % 360
    } else {
      // Base: turn toward runway
      heading = (rwy.heading_deg + (p.lateral > 0 ? -90 : 90) + 360) % 360
    }
  }

  return {
    lat_deg: rwy.lat_deg + n_nm * NM_TO_DEG_LAT,
    lon_deg: rwy.lon_deg + (e_nm * NM_TO_DEG_LAT) / cos_lat,
    alt_m,
    heading_rad: heading * DEG2RAD,
    airspeed_ms: p.kt * KT2MS,
    heading_deg: heading,
  }
}

function PositionIcons({ rwyEnd, airport, selected, onSelect }) {
  if (!rwyEnd) return null

  return (
    <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 8, justifyContent: 'center' }}>
      {POSITIONS.map((p) => {
        const active = selected === p.id
        const c = computePosition(rwyEnd, airport, p)
        return (
          <button key={p.id}
            onClick={() => onSelect(p.id, c)}
            style={{
              width: 68, padding: '8px 2px', borderRadius: 4,
              display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 3,
              border: `1px solid ${active ? '#00ff88' : '#1e293b'}`,
              background: active ? 'rgba(0,255,136,0.10)' : '#1c2333',
              cursor: 'pointer', touchAction: 'manipulation',
            }}
          >
            <AircraftIcon rotation={c.heading_deg} size={22}
              color={active ? '#00ff88' : '#64748b'} />
            <span style={{
              fontSize: 11, fontWeight: 700, fontFamily: 'monospace',
              color: active ? '#00ff88' : '#e2e8f0',
            }}>{p.label}</span>
            <span style={{ fontSize: 9, fontFamily: 'monospace', color: '#475569' }}>
              {p.kt > 0 ? `${p.kt}kt` : 'GND'}
            </span>
          </button>
        )
      })}
    </div>
  )
}

// ─── Numpad-backed field for manual reposition inputs ────────────────────────
function NumpadField({ label, value, placeholder, allowDecimal = true, hint = '', onChange }) {
  const [open, setOpen] = useState(false)
  return (
    <label style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace' }}>{label}
      <div
        style={{
          ...inputBase, cursor: 'pointer',
          display: 'flex', alignItems: 'center',
          color: value ? '#e2e8f0' : '#475569',
        }}
        onClick={() => setOpen(true)}
      >
        {value || placeholder}
      </div>
      {open && (
        <NumpadPopup
          label={label}
          hint={hint}
          value={value || ''}
          allowDecimal={allowDecimal}
          onSubmit={(v) => { onChange(v); setOpen(false) }}
          onCancel={() => setOpen(false)}
        />
      )}
    </label>
  )
}

// ─── Main panel ──────────────────────────────────────────────────────────────
export default function PositionPanel() {
  const { fdm, sendCommand, requestAction, pendingAction,
          getRunways, setDeparture, runwayResults,
          icConfiguration, setIcConfiguration } = useSimStore(useShallow(s => ({
    fdm: s.fdm, sendCommand: s.sendCommand, requestAction: s.requestAction,
    pendingAction: s.pendingAction, getRunways: s.getRunways, setDeparture: s.setDeparture,
    runwayResults: s.runwayResults, icConfiguration: s.icConfiguration,
    setIcConfiguration: s.setIcConfiguration,
  })))

  const [depAirport, setDepAirport] = useState(null)
  const [destAirport, setDestAirport] = useState(null)
  const [depRunway, setDepRunway] = useState(null)
  const [pos, setPos] = useState({ lat: '', lon: '', alt: '', hdg: '' })

  useEffect(() => {
    if (depAirport?.icao) {
      getRunways(depAirport.icao)
      setDepRunway(null)
    }
  }, [depAirport?.icao])

  const activeAirport = (runwayResults?.icao === depAirport?.icao)
    ? runwayResults : depAirport

  const [selectedPos, setSelectedPos] = useState(null)
  const [pendingIC, setPendingIC] = useState(null)

  // Reset position selection when runway changes
  useEffect(() => {
    setSelectedPos(null)
    setPendingIC(null)
  }, [depRunway?.designator])

  const handlePositionSelect = (posId, computed) => {
    setSelectedPos(posId)
    setPendingIC(computed)
  }

  const handlePositionApply = () => {
    if (!pendingIC) return
    setDeparture({
      lat_deg: pendingIC.lat_deg,
      lon_deg: pendingIC.lon_deg,
      alt_m: pendingIC.alt_m,
      heading_rad: pendingIC.heading_rad,
      airspeed_ms: pendingIC.airspeed_ms,
    })
  }

  const setPosition = () => {
    const data = {}
    if (pos.lat) data.lat = Number(pos.lat)
    if (pos.lon) data.lon = Number(pos.lon)
    if (pos.alt) data.alt_ft = Number(pos.alt)
    if (pos.hdg) data.hdg_deg = Number(pos.hdg)
    sendCommand(13, data)
  }

  return (
    <div>
      <SectionHeader title="CURRENT POSITION" />
      <PanelRow label="LAT" value={fdm.lat.toFixed(6)} unit="°" />
      <PanelRow label="LON" value={fdm.lon.toFixed(6)} unit="°" />
      <PanelRow label="ALT" value={fdm.altFtMsl.toFixed(0)} unit="ft MSL" />

      <SectionHeader title="DEPARTURE" />

      {/* IC Configuration selector */}
      <div style={{ display: 'flex', gap: 4, marginBottom: 8 }}>
        {[
          { key: 'cold_and_dark', label: 'COLD & DARK' },
          { key: 'ready_for_takeoff', label: 'READY T/O' },
          { key: 'airborne_clean', label: 'AIRBORNE' },
        ].map(opt => (
          <button key={opt.key} onClick={() => setIcConfiguration(opt.key)} style={{
            flex: 1, padding: '5px 4px', fontSize: 10, fontWeight: 600,
            fontFamily: "'JetBrains Mono', monospace",
            border: `1px solid ${icConfiguration === opt.key ? '#00ff88' : '#1e293b'}`,
            borderRadius: 4, cursor: 'pointer',
            background: icConfiguration === opt.key ? 'rgba(0,255,136,0.12)' : '#111827',
            color: icConfiguration === opt.key ? '#00ff88' : '#64748b',
          }}>
            {opt.label}
          </button>
        ))}
      </div>

      <AirportSearch value={depAirport} onSelect={setDepAirport} />

      {activeAirport?.runways?.length > 0 && (
        <>
          <div style={{ fontSize: 11, color: '#64748b', fontFamily: 'monospace', marginTop: 10 }}>
            RUNWAY
          </div>
          <RunwaySelector airport={activeAirport} selected={depRunway} onSelect={(rwy) => {
            console.log('[POS] runway selected:', rwy.designator,
              'lat_deg=', rwy.lat_deg, 'lon_deg=', rwy.lon_deg,
              'hdg=', rwy.heading_deg)
            setDepRunway(rwy)
          }} />
        </>
      )}

      {depRunway && (
        <>
          <div style={{ fontSize: 11, color: '#64748b', fontFamily: 'monospace', marginTop: 14 }}>
            POSITION
          </div>
          <PositionIcons rwyEnd={depRunway} airport={activeAirport}
            selected={selectedPos} onSelect={handlePositionSelect} />
          {pendingIC && (
            <div style={{ marginTop: 10 }}>
              <div style={{ fontSize: 10, color: '#475569', fontFamily: 'monospace', marginBottom: 6 }}>
                {pendingIC.lat_deg.toFixed(4)}° / {pendingIC.lon_deg.toFixed(4)}°
                &nbsp; {Math.round(pendingIC.alt_m * 3.28084)}ft
                &nbsp; HDG {Math.round(pendingIC.heading_deg)}°
                &nbsp; {Math.round(pendingIC.airspeed_ms * 1.94384)}kt
              </div>
              <FullWidthBtn
                label={pendingAction?.type === 'set_departure' ? 'CONFIRM? (tap again)' : 'SET POSITION'}
                style={pendingAction?.type === 'set_departure'
                  ? { background: 'rgba(0,255,136,0.12)', color: '#00ff88', borderColor: '#00ff88' }
                  : { background: '#1c2333', color: '#e2e8f0', borderColor: '#1e293b' }}
                onClick={() => requestAction('set_departure', handlePositionApply)}
              />
            </div>
          )}
        </>
      )}

      <SectionHeader title="DESTINATION" />
      <AirportSearch value={destAirport} onSelect={setDestAirport} />

      <SectionHeader title="MANUAL REPOSITION" />
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8, marginBottom: 12 }}>
        <NumpadField label="LAT" value={pos.lat} placeholder={fdm.lat.toFixed(4)}
          allowDecimal hint="degrees"
          onChange={(v) => setPos(p => ({ ...p, lat: v }))} />
        <NumpadField label="LON" value={pos.lon} placeholder={fdm.lon.toFixed(4)}
          allowDecimal hint="degrees"
          onChange={(v) => setPos(p => ({ ...p, lon: v }))} />
        <NumpadField label="ALT (ft)" value={pos.alt} placeholder={fdm.altFtMsl.toFixed(0)}
          allowDecimal={false} hint="feet"
          onChange={(v) => setPos(p => ({ ...p, alt: v }))} />
        <NumpadField label="HDG (\u00B0)" value={pos.hdg} placeholder={fdm.hdgTrueDeg.toFixed(0)}
          allowDecimal={false} hint="0\u2013360"
          onChange={(v) => setPos(p => ({ ...p, hdg: v }))} />
      </div>
      <FullWidthBtn
        label={pendingAction?.type === 'reposition' ? 'CONFIRM? (tap again)' : 'SET POSITION'}
        style={pendingAction?.type === 'reposition'
          ? { background: 'rgba(188, 79, 203, 0.12)', color: '#bc4fcb', borderColor: '#bc4fcb' }
          : { background: '#1c2333', color: '#64748b', borderColor: '#1e293b' }}
        onClick={() => requestAction('reposition', setPosition)}
      />
    </div>
  )
}
