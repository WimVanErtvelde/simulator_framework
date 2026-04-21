import { useEffect, useState } from 'react'
import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import MicroburstControls, { INTENSITY_MS, intensityLabelFor } from './MicroburstControls'
import RunwaySelector from './RunwaySelector'

// Microburst authoring subsection rendered inside a patch tab's right
// column. Airport patches get a runway picker + distance-from-threshold.
// Custom patches get bearing-from-center + distance.
//
// Patch must have a server-assigned patch_id before a microburst can be
// attached — the backend keys set_patch_microburst on patch_id. We gate
// the ADD button on patch.patch_id != null and show a hint when the
// patch is still mid-add (next Accept will assign an id).

export default function PatchMicroburstSection({ patch }) {
  const setPatchMicroburst   = useWeatherV2Store(s => s.setPatchMicroburst)
  const clearPatchMicroburst = useWeatherV2Store(s => s.clearPatchMicroburst)

  const mb = patch.microburst
  const isAirport = patch.patch_type === 'airport'
  const hasPatchId = patch.patch_id != null

  const [selectedEnd, setSelectedEnd] = useState(null)
  const [intensity, setIntensity] = useState(mb ? intensityLabelFor(mb.intensity) : 'Med')
  const [diameterM, setDiameterM] = useState(mb ? Math.round(mb.core_radius_m * 2) : 2000)
  const [distanceNm, setDistanceNm] = useState(3)
  const [bearingDeg, setBearingDeg] = useState(0)

  // When server-side microburst changes (from broadcast), re-seed the
  // local controls so the UI reflects reality. Keep distance/bearing
  // local-only since server doesn't carry them (author-side projection).
  useEffect(() => {
    if (!mb) return
    setIntensity(intensityLabelFor(mb.intensity))
    setDiameterM(Math.round(mb.core_radius_m * 2))
  }, [mb?.intensity, mb?.core_radius_m])

  const canActivate = hasPatchId &&
    (isAirport ? !!selectedEnd : true)

  const activate = () => {
    if (!canActivate) return
    const coords = isAirport && selectedEnd
      ? projectAlongThreshold(selectedEnd, distanceNm)
      : projectFromCenter(patch.lat_deg, patch.lon_deg, bearingDeg, distanceNm)
    setPatchMicroburst(patch.client_id, {
      latitude_deg:     coords.lat,
      longitude_deg:    coords.lon,
      core_radius_m:    diameterM / 2,
      intensity:        INTENSITY_MS[intensity],
      shaft_altitude_m: 300,
    })
  }

  const clear = () => clearPatchMicroburst(patch.client_id)

  return (
    <div style={cardStyle}>
      <div style={sectionHeader}>MICROBURST</div>

      {!hasPatchId && (
        <div style={noticeStyle}>
          Accept the patch first — microburst needs a server-assigned
          patch id.
        </div>
      )}

      {isAirport && (
        <div style={{ marginBottom: 8 }}>
          <div style={fieldLabel}>RUNWAY END</div>
          <RunwaySelector
            icao={patch.icao}
            value={selectedEnd}
            onSelect={setSelectedEnd}
          />
        </div>
      )}

      <MicroburstControls
        intensity={intensity}   onIntensityChange={setIntensity}
        diameterM={diameterM}   onDiameterChange={setDiameterM}
        distanceNm={distanceNm} onDistanceChange={setDistanceNm}
        showDistance={isAirport}
        bearingDeg={bearingDeg} onBearingChange={setBearingDeg}
        showBearing={!isAirport}
      />

      <div style={{ display: 'flex', gap: 8, marginTop: 10 }}>
        <button type="button"
          onClick={activate}
          onTouchEnd={(e) => { e.preventDefault(); activate() }}
          disabled={!canActivate}
          style={activateBtn(canActivate)}
        >{mb ? 'UPDATE' : 'ADD'} MICROBURST</button>
        {mb && (
          <button type="button"
            onClick={clear}
            onTouchEnd={(e) => { e.preventDefault(); clear() }}
            style={clearBtn}
          >REMOVE</button>
        )}
      </div>

      {mb && (
        <div style={summaryStyle}>
          Active · R={Math.round(mb.core_radius_m)}m ·
          λ={mb.intensity.toFixed(1)} m/s ·
          {mb.latitude_deg.toFixed(4)}°, {mb.longitude_deg.toFixed(4)}°
        </div>
      )}
    </div>
  )
}

// Runway approach is OPPOSITE to landing-roll heading: aircraft on
// short final approaches FROM the threshold side with heading =
// runway_heading, so the patch's "positive distance" means before
// reaching the threshold along the approach course = (heading + 180).
// Heading is magnetic — ~5° worst-case error in Europe, NM-negligible.
function projectAlongThreshold(runwayEnd, distanceNm) {
  const bearingFromThreshold = (runwayEnd.heading_deg + 180) % 360
  return projectFromCenter(
    runwayEnd.threshold_lat_deg,
    runwayEnd.threshold_lon_deg,
    bearingFromThreshold,
    distanceNm,
  )
}

function projectFromCenter(lat, lon, bearingDeg, distanceNm) {
  const NM_PER_DEG = 60
  const bearingRad = bearingDeg * Math.PI / 180
  const latRad = lat * Math.PI / 180
  return {
    lat: lat + (distanceNm / NM_PER_DEG) * Math.cos(bearingRad),
    lon: lon + (distanceNm / NM_PER_DEG) * Math.sin(bearingRad) / Math.cos(latRad),
  }
}

const cardStyle = {
  padding: 12,
  background: '#0d1117',
  border: '1px solid #1e293b',
  borderRadius: 3,
  display: 'flex', flexDirection: 'column',
}

const sectionHeader = {
  fontFamily: 'monospace',
  fontSize: 11, fontWeight: 700,
  letterSpacing: 2,
  color: '#bc4fcb',
  textTransform: 'uppercase',
  padding: '2px 0 8px',
  borderBottom: '1px solid #1e293b',
  marginBottom: 10,
}

const fieldLabel = {
  fontFamily: 'monospace',
  fontSize: 10, fontWeight: 700,
  letterSpacing: 1,
  color: '#64748b',
  marginBottom: 4,
  textTransform: 'uppercase',
}

const noticeStyle = {
  fontSize: 11, color: '#f59e0b',
  fontFamily: 'monospace', fontStyle: 'italic',
  padding: '6px 8px', marginBottom: 10,
  border: '1px dashed #475569', borderRadius: 3,
  background: '#1c1710',
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
  marginTop: 10,
  padding: '6px 8px',
  fontSize: 10, color: '#94a3b8',
  fontFamily: 'monospace',
  border: '1px solid #1e293b',
  borderRadius: 3,
  background: '#111827',
}
