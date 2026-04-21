import { useEffect } from 'react'
import { useSimStore } from '../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'

// Runway end picker for microburst placement.
//
// Given an `icao`, fetches runway data via the existing useSimStore
// getRunways/runwayResults plumbing (shared with PositionPanel). Each
// physical runway strip has two ends (e.g. 07L / 25R); we flatten into
// 2N pickable buttons grouped visually by strip so the instructor can
// see which approach direction they're picking.
//
// onSelect receives a flattened end object:
//   { ident, threshold_lat_deg, threshold_lon_deg, heading_deg,
//     displaced_threshold_m, elevation_m, length_m, strip_idx, end }
//
// Note: heading_deg is MAGNETIC (per Runway.msg). Microburst placement
// is NM-scale so a ~5° magvar error is visually negligible.

export default function RunwaySelector({ icao, value, onSelect }) {
  const { getRunways, runwayResults, wsConnected } = useSimStore(useShallow(s => ({
    getRunways:    s.getRunways,
    runwayResults: s.runwayResults,
    wsConnected:   s.wsConnected,
  })))

  useEffect(() => {
    if (!icao || !wsConnected) return
    if (runwayResults?.icao === icao) return
    getRunways(icao)
  }, [icao, wsConnected])

  if (!icao) {
    return <div style={emptyStyle}>Select an airport first</div>
  }
  if (runwayResults?.icao !== icao || !runwayResults?.runways?.length) {
    return <div style={emptyStyle}>Loading runways…</div>
  }

  const strips = runwayResults.runways.map((rwy, idx) => {
    const elev = rwy.elevation_m || runwayResults.elevation_m || 0
    const ends = []
    if (rwy.designator_end1) {
      ends.push({
        ident:                 rwy.designator_end1,
        threshold_lat_deg:     rwy.threshold_lat_deg_end1,
        threshold_lon_deg:     rwy.threshold_lon_deg_end1,
        heading_deg:           rwy.heading_deg_end1,
        displaced_threshold_m: rwy.displaced_threshold_m_end1,
        elevation_m:           elev,
        length_m:              rwy.length_m,
        strip_idx:             idx,
        end:                   1,
      })
    }
    if (rwy.designator_end2) {
      ends.push({
        ident:                 rwy.designator_end2,
        threshold_lat_deg:     rwy.threshold_lat_deg_end2,
        threshold_lon_deg:     rwy.threshold_lon_deg_end2,
        heading_deg:           rwy.heading_deg_end2,
        displaced_threshold_m: rwy.displaced_threshold_m_end2,
        elevation_m:           elev,
        length_m:              rwy.length_m,
        strip_idx:             idx,
        end:                   2,
      })
    }
    return ends
  }).filter(ends => ends.length > 0)

  return (
    <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8, marginTop: 6 }}>
      {strips.map((ends, stripIdx) => (
        <div key={stripIdx} style={stripGroupStyle}>
          {ends.map((e, i) => {
            const active = value?.ident === e.ident &&
                           value?.strip_idx === e.strip_idx
            return (
              <RunwayButton key={e.ident} end={e} active={active}
                onSelect={onSelect} />
            )
          }).reduce((acc, btn, i) => {
            if (i > 0) acc.push(<span key={`sep-${i}`} style={sepStyle}>·</span>)
            acc.push(btn)
            return acc
          }, [])}
        </div>
      ))}
    </div>
  )
}

function RunwayButton({ end, active, onSelect }) {
  return (
    <button
      type="button"
      onClick={() => onSelect(end)}
      onTouchEnd={(e) => { e.preventDefault(); onSelect(end) }}
      style={{
        padding: '6px 12px',
        fontFamily: 'monospace',
        fontSize: 12, fontWeight: 700,
        letterSpacing: 1,
        border: `1px solid ${active ? '#39d0d8' : '#1e293b'}`,
        background: active ? 'rgba(57, 208, 216, 0.12)' : '#111827',
        color: active ? '#39d0d8' : '#94a3b8',
        borderRadius: 3,
        cursor: 'pointer',
        touchAction: 'manipulation',
      }}
    >{end.ident}</button>
  )
}

const stripGroupStyle = {
  display: 'inline-flex', alignItems: 'center', gap: 4,
  padding: '2px',
  border: '1px dashed #1e293b',
  borderRadius: 4,
}

const sepStyle = {
  color: '#334155', fontFamily: 'monospace', fontSize: 10,
  padding: '0 1px',
}

const emptyStyle = {
  fontSize: 11, color: '#475569',
  fontFamily: 'monospace', fontStyle: 'italic',
  padding: '6px 0',
}
