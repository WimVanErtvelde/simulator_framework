import { useState } from 'react'
import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import AirportSearch from '../../ui/AirportSearch'
import { M_TO_FT } from './graph/mslScale'

const NM_TO_M = 1852
const RADIUS_MIN_NM = 1
const RADIUS_MAX_NM = 50

// Top strip of a PatchTab once an airport is set. Holds the airport
// picker, ground elevation readout, radius slider, (custom-only) label
// input, and a delete button.
//
// Identity fields (icao, radius, label) commit on user-release events.
// During a drag or typing burst, the in-flight value lives in LOCAL
// component state only — draft and serverState are never touched until
// the user releases. This keeps the global Accept button from flickering
// active/inactive on every slider tick. On commit, updatePatchIdentity
// fires update_patch_identity and mirrors to both draft and serverState
// in one step, so creation never registers as a dirty edit.
//   - AirportSearch onSelect             (discrete event; fires immediately)
//   - Radius slider onMouseUp/onTouchEnd (fires on drag release)
//   - Label input onBlur / Enter         (fires on focus loss)
// removePatch fires remove_patch immediately.
export default function PatchHeader({ patch }) {
  const updatePatchIdentity = useWeatherV2Store(s => s.updatePatchIdentity)
  const removePatch         = useWeatherV2Store(s => s.removePatch)

  // Transient local state for in-flight edits. `null` means "follow store
  // value"; a non-null value means the user is actively editing and the
  // display should reflect their in-flight value (draft stays clean).
  const [pendingRadiusNm, setPendingRadiusNm] = useState(null)
  const [pendingLabel,    setPendingLabel]    = useState(null)

  const storeRadiusNm = Math.round(((patch.radius_m || 0) / NM_TO_M) * 10) / 10
  const displayRadiusNm = pendingRadiusNm ?? storeRadiusNm
  const displayLabel    = pendingLabel    ?? patch.label
  const groundFt = Math.round((patch.ground_elevation_m || 0) * M_TO_FT)

  const onSelectAirport = (apt) => {
    if (!apt) return
    updatePatchIdentity(patch.client_id, {
      icao:               apt.icao,
      lat_deg:            apt.arp_lat_deg ?? 0,
      lon_deg:            apt.arp_lon_deg ?? 0,
      ground_elevation_m: apt.elevation_m  ?? 0,
    })
  }

  const onDelete = () => removePatch(patch.client_id)

  // Commit the in-flight radius on drag release. If the user didn't move
  // the slider at all (pendingRadiusNm still null), skip — no change to
  // commit and no redundant WS message.
  const commitRadius = () => {
    if (pendingRadiusNm == null) return
    updatePatchIdentity(patch.client_id, { radius_m: pendingRadiusNm * NM_TO_M })
    setPendingRadiusNm(null)
  }

  // Commit the in-flight label on blur / Enter. No-op if unchanged.
  const commitLabel = () => {
    if (pendingLabel == null || pendingLabel === patch.label) {
      setPendingLabel(null)
      return
    }
    updatePatchIdentity(patch.client_id, { label: pendingLabel })
    setPendingLabel(null)
  }

  return (
    <div style={{
      display: 'flex', alignItems: 'center', gap: 16,
      padding: 12, background: '#0d1117',
      border: '1px solid #1e293b', borderRadius: 3,
      flexWrap: 'wrap',
    }}>
      {/* Airport */}
      <div style={{ flex: '0 1 260px', minWidth: 220 }}>
        <div style={fieldTagStyle}>AIRPORT</div>
        <AirportSearch
          value={{ icao: patch.icao, name: patch.icao }}
          onSelect={onSelectAirport}
          placeholder="ICAO..."
        />
      </div>

      {/* Label (editable for custom only). In-flight value lives in local
          state; committed via updatePatchIdentity on blur / Enter. */}
      {patch.role === 'custom' && (
        <div style={{ flex: '0 1 180px', minWidth: 140 }}>
          <div style={fieldTagStyle}>LABEL</div>
          <input
            type="text"
            value={displayLabel}
            onChange={e => setPendingLabel(e.target.value)}
            onBlur={commitLabel}
            onKeyDown={e => { if (e.key === 'Enter') e.currentTarget.blur() }}
            style={labelInputStyle}
          />
        </div>
      )}

      {/* Ground elevation readout */}
      <div>
        <div style={fieldTagStyle}>GROUND</div>
        <div style={{
          fontFamily: 'monospace', color: '#94a3b8',
          fontSize: 13, fontVariantNumeric: 'tabular-nums', height: 44,
          display: 'flex', alignItems: 'center',
        }}>
          {groundFt.toLocaleString()} ft
        </div>
      </div>

      {/* Radius */}
      <div style={{ flex: '1 1 200px', minWidth: 180 }}>
        <div style={fieldTagStyle}>RADIUS</div>
        <div style={{
          display: 'flex', alignItems: 'center', gap: 8, height: 44,
        }}>
          <input type="range"
            min={RADIUS_MIN_NM} max={RADIUS_MAX_NM} step={0.5}
            value={displayRadiusNm}
            onChange={e => setPendingRadiusNm(Number(e.target.value))}
            onMouseUp={commitRadius}
            onTouchEnd={commitRadius}
            onKeyUp={commitRadius}
            style={{ flex: 1, accentColor: '#39d0d8', touchAction: 'manipulation' }}
          />
          <span style={{
            fontFamily: 'monospace', color: '#e2e8f0',
            fontSize: 12, fontVariantNumeric: 'tabular-nums',
            minWidth: 56, textAlign: 'right',
          }}>
            {displayRadiusNm.toFixed(1)} NM
          </span>
        </div>
      </div>

      {/* Delete */}
      <button
        type="button"
        onClick={onDelete}
        onTouchEnd={(e) => { e.preventDefault(); onDelete() }}
        style={deleteBtnStyle}
      >× DELETE</button>
    </div>
  )
}

const fieldTagStyle = {
  fontSize: 10, color: '#64748b',
  fontFamily: 'monospace', letterSpacing: 1, marginBottom: 4,
  textTransform: 'uppercase',
}

const labelInputStyle = {
  width: '100%', height: 44, padding: '8px 12px',
  background: '#1c2333', border: '1px solid #1e293b', borderRadius: 3,
  color: '#e2e8f0', fontFamily: 'monospace', fontSize: 13,
  WebkitAppearance: 'none', outline: 'none', boxSizing: 'border-box',
}

const deleteBtnStyle = {
  height: 44, padding: '0 16px',
  background: 'rgba(239, 68, 68, 0.06)',
  border: '1px solid #7f1d1d',
  borderRadius: 3,
  color: '#f87171',
  fontFamily: 'monospace', fontSize: 11, fontWeight: 700,
  letterSpacing: 1, textTransform: 'uppercase',
  cursor: 'pointer', touchAction: 'manipulation',
  alignSelf: 'flex-end',
}
