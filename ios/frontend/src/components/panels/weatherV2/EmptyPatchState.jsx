import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import AirportSearch from '../../ui/AirportSearch'

// Shown when a patch tab is active but the patch has no airport yet
// (dep/dest: instructor just clicked +DEP but hasn't picked a station;
// custom: just created). Prompts for airport selection. Cancel removes
// the patch and returns to Global.
//
// AirportSearch's onSelect hands back the full airport dict:
// {icao, name, city, country, iata, arp_lat_deg, arp_lon_deg,
//  elevation_m, transition_altitude_ft, runways[]}.
export default function EmptyPatchState({ patch }) {
  const updatePatch  = useWeatherV2Store(s => s.updatePatch)
  const removePatch  = useWeatherV2Store(s => s.removePatch)
  const setActiveTab = useWeatherV2Store(s => s.setActiveTab)

  const onSelectAirport = (apt) => {
    if (!apt) return
    updatePatch(patch.client_id, {
      icao:               apt.icao,
      lat_deg:            apt.arp_lat_deg ?? 0,
      lon_deg:            apt.arp_lon_deg ?? 0,
      ground_elevation_m: apt.elevation_m  ?? 0,
      // Custom tabs display the ICAO as their label by default; dep/dest
      // keep their DEPARTURE/DESTINATION titles.
      label: patch.role === 'custom' ? apt.icao : patch.label,
    })
  }

  const onCancel = () => {
    setActiveTab('global')
    removePatch(patch.client_id)
  }

  const title =
    patch.role === 'departure'   ? 'DEPARTURE PATCH' :
    patch.role === 'destination' ? 'DESTINATION PATCH' :
                                   'CUSTOM PATCH'

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
        Pick an airport to configure this patch.
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
