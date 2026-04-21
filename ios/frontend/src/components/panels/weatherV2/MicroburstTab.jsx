import { useEffect, useState } from 'react'
import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import AirportSearch from '../../ui/AirportSearch'
import RunwaySelector from './RunwaySelector'
import MicroburstControls, { INTENSITY_MS, intensityLabelFor } from './MicroburstControls'

// Standalone microburst tab — authors a microburst not tied to any
// weather patch (source_patch_id = 0 on the wire). Airport + runway
// picker for placement, shared authoring controls via MicroburstControls.
// At most one standalone microburst at a time; backend enforces same.

export default function MicroburstTab() {
  const setStandalone   = useWeatherV2Store(s => s.setStandaloneMicroburst)
  const clearStandalone = useWeatherV2Store(s => s.clearStandaloneMicroburst)
  const standalone      = useWeatherV2Store(s => s.draft.microbursts.standalone)

  const [airport, setAirport] = useState(null)
  const [selectedEnd, setSelectedEnd] = useState(null)
  const [intensity, setIntensity] = useState(
    standalone ? intensityLabelFor(standalone.intensity) : 'Med'
  )
  const [diameterM, setDiameterM] = useState(
    standalone ? Math.round(standalone.core_radius_m * 2) : 2000
  )
  const [distanceNm, setDistanceNm] = useState(3)

  // Re-seed when server-side standalone changes.
  useEffect(() => {
    if (!standalone) return
    setIntensity(intensityLabelFor(standalone.intensity))
    setDiameterM(Math.round(standalone.core_radius_m * 2))
  }, [standalone?.intensity, standalone?.core_radius_m])

  // Picking a new airport invalidates any runway selection.
  const pickAirport = (apt) => {
    setAirport(apt)
    setSelectedEnd(null)
  }

  const canActivate = !!selectedEnd

  const activate = () => {
    if (!canActivate) return
    const bearingFromThreshold = (selectedEnd.heading_deg + 180) % 360
    const NM_PER_DEG = 60
    const bearingRad = bearingFromThreshold * Math.PI / 180
    const latRad = selectedEnd.threshold_lat_deg * Math.PI / 180
    const lat = selectedEnd.threshold_lat_deg +
                (distanceNm / NM_PER_DEG) * Math.cos(bearingRad)
    const lon = selectedEnd.threshold_lon_deg +
                (distanceNm / NM_PER_DEG) * Math.sin(bearingRad) / Math.cos(latRad)

    setStandalone({
      latitude_deg:     lat,
      longitude_deg:    lon,
      core_radius_m:    diameterM / 2,
      intensity:        INTENSITY_MS[intensity],
      shaft_altitude_m: 300,
    })
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12, fontFamily: 'monospace' }}>
      <div style={headerStyle}>STANDALONE MICROBURST</div>
      <div style={hintStyle}>
        Runway-relative microburst placement not tied to a weather patch.
        Commit via Accept; the active microburst is broadcast as
        source_patch_id = 0.
      </div>

      <div style={cardStyle}>
        <div style={fieldLabel}>AIRPORT</div>
        <AirportSearch value={airport} onSelect={pickAirport} />

        {airport && (
          <>
            <div style={{ marginTop: 12 }}>
              <div style={fieldLabel}>RUNWAY END</div>
              <RunwaySelector
                icao={airport.icao}
                value={selectedEnd}
                onSelect={setSelectedEnd}
              />
            </div>

            <div style={{ marginTop: 12 }}>
              <MicroburstControls
                intensity={intensity}   onIntensityChange={setIntensity}
                diameterM={diameterM}   onDiameterChange={setDiameterM}
                distanceNm={distanceNm} onDistanceChange={setDistanceNm}
                showDistance={true}
                bearingDeg={0} onBearingChange={() => {}}
                showBearing={false}
              />
            </div>

            <div style={{ display: 'flex', gap: 8, marginTop: 12 }}>
              <button type="button"
                onClick={activate}
                onTouchEnd={(e) => { e.preventDefault(); activate() }}
                disabled={!canActivate}
                style={activateBtn(canActivate)}
              >{standalone ? 'UPDATE' : 'ADD'} MICROBURST</button>
              {standalone && (
                <button type="button"
                  onClick={clearStandalone}
                  onTouchEnd={(e) => { e.preventDefault(); clearStandalone() }}
                  style={clearBtn}
                >REMOVE</button>
              )}
            </div>
          </>
        )}
      </div>

      {standalone && (
        <div style={summaryStyle}>
          <strong style={{ color: '#bc4fcb' }}>Active:</strong>
          {' '}R={Math.round(standalone.core_radius_m)}m ·
          λ={standalone.intensity.toFixed(1)} m/s ·
          {standalone.latitude_deg.toFixed(4)}°,
          {' '}{standalone.longitude_deg.toFixed(4)}°
        </div>
      )}
    </div>
  )
}

const headerStyle = {
  fontSize: 11, fontWeight: 700, letterSpacing: 2,
  color: '#39d0d8', textTransform: 'uppercase',
  padding: '4px 0 6px', borderBottom: '1px solid #1e293b',
}

const hintStyle = {
  fontSize: 11, color: '#94a3b8', lineHeight: 1.5,
  fontStyle: 'italic',
}

const cardStyle = {
  padding: 12,
  background: '#0d1117',
  border: '1px solid #1e293b',
  borderRadius: 3,
  maxWidth: 480,
}

const fieldLabel = {
  fontFamily: 'monospace',
  fontSize: 10, fontWeight: 700,
  letterSpacing: 1,
  color: '#64748b',
  marginBottom: 4,
  textTransform: 'uppercase',
}

const activateBtn = (enabled) => ({
  flex: 1,
  padding: '8px 0',
  fontFamily: 'monospace',
  fontSize: 11, fontWeight: 700,
  letterSpacing: 1, textTransform: 'uppercase',
  border: `1px solid ${enabled ? '#bc4fcb' : '#1e293b'}`,
  background: enabled ? 'rgba(188, 79, 203, 0.10)' : '#111827',
  color: enabled ? '#bc4fcb' : '#334155',
  borderRadius: 3,
  cursor: enabled ? 'pointer' : 'not-allowed',
  touchAction: 'manipulation',
  opacity: enabled ? 1 : 0.5,
})

const clearBtn = {
  padding: '8px 12px',
  fontFamily: 'monospace',
  fontSize: 11, fontWeight: 700,
  letterSpacing: 1, textTransform: 'uppercase',
  border: '1px solid #1e293b',
  background: '#111827',
  color: '#94a3b8',
  borderRadius: 3,
  cursor: 'pointer',
  touchAction: 'manipulation',
}

const summaryStyle = {
  padding: '8px 12px',
  fontSize: 11, color: '#94a3b8',
  fontFamily: 'monospace',
  border: '1px solid #1e293b',
  borderRadius: 3,
  background: '#111827',
  maxWidth: 480,
}
