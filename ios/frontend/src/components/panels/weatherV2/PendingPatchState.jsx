import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import AirportSearch from '../../ui/AirportSearch'

// Rendered when the user has clicked +DEP or +DEST but hasn't picked
// an airport yet. Under 5c-refactor-I, creation is deferred until the
// airport is picked — no draft patch with an empty icao. On select,
// addPatch fires reserve_patch immediately; the tab pivots to the new
// client_id and the role drops out of pendingTabs.
//
// AirportSearch hands back the full airport dict
// ({icao, arp_lat_deg, arp_lon_deg, elevation_m, ...}); we pass
// elevation_m through so the backend doesn't need to re-resolve via
// SearchAirports.
export default function PendingPatchState({ role }) {
  const addPatch         = useWeatherV2Store(s => s.addPatch)
  const closePendingTab  = useWeatherV2Store(s => s.closePendingTab)

  const onSelectAirport = (apt) => {
    if (!apt) return
    addPatch(role, {
      icao:               apt.icao,
      lat_deg:            apt.arp_lat_deg ?? 0,
      lon_deg:            apt.arp_lon_deg ?? 0,
      ground_elevation_m: apt.elevation_m  ?? 0,
    })
    closePendingTab(role)
  }

  const onCancel = () => closePendingTab(role)

  const title =
    role === 'departure'   ? 'DEPARTURE PATCH' :
    role === 'destination' ? 'DESTINATION PATCH' :
                             'PATCH'

  return (
    <div style={{
      display: 'flex', flexDirection: 'column',
      alignItems: 'center', justifyContent: 'center',
      padding: 48, gap: 16,
      minHeight: 400,
    }}>
      <div style={{
        fontSize: 14, color: '#94a3b8', fontFamily: 'monospace',
        letterSpacing: 1, textTransform: 'uppercase', fontWeight: 700,
      }}>{title}</div>
      <div style={{ color: '#64748b', fontSize: 13, fontFamily: 'monospace' }}>
        Pick an airport to reserve this patch.
      </div>
      <div style={{ width: 320 }}>
        <AirportSearch
          value={null}
          onSelect={onSelectAirport}
          placeholder="ICAO or airport name"
        />
      </div>
      <button
        type="button"
        onClick={onCancel}
        onTouchEnd={(e) => { e.preventDefault(); onCancel() }}
        style={{
          marginTop: 4, height: 28, padding: '0 12px',
          background: '#111827',
          border: '1px solid #1e293b',
          borderRadius: 3,
          color: '#94a3b8',
          fontFamily: 'monospace', fontSize: 11, fontWeight: 700,
          letterSpacing: 1, textTransform: 'uppercase',
          cursor: 'pointer', touchAction: 'manipulation',
        }}
      >Cancel</button>
    </div>
  )
}
