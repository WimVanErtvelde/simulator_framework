import { useEffect, useMemo } from 'react'
import { MapContainer, TileLayer, Marker, Polyline, useMap } from 'react-leaflet'
import L from 'leaflet'
import { useSimStore } from '../store/useSimStore'

// Fix Leaflet default icon
import iconUrl from 'leaflet/dist/images/marker-icon.png'
import iconRetinaUrl from 'leaflet/dist/images/marker-icon-2x.png'
import shadowUrl from 'leaflet/dist/images/marker-shadow.png'
delete L.Icon.Default.prototype._getIconUrl
L.Icon.Default.mergeOptions({ iconUrl, iconRetinaUrl, shadowUrl })

function createAircraftIcon(hdgDeg, simState, isHelicopter) {
  const color = simState === 'RUNNING' ? '#2563eb'
              : simState === 'FROZEN'  ? '#ef4444'
              : '#8b949e'
  const outline = simState === 'RUNNING' ? '#1d4ed8'
                : simState === 'FROZEN'  ? '#b91c1c'
                : '#6b7280'

  const fixedWingSvg = `
    <svg width="48" height="48" viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg">
      <!-- fuselage -->
      <path d="M24 4 C22.5 4 21.5 6 21.5 8 L21.5 34 C21.5 36 22.5 40 24 42 C25.5 40 26.5 36 26.5 34 L26.5 8 C26.5 6 25.5 4 24 4Z" fill="${color}" stroke="${outline}" stroke-width="0.8"/>
      <!-- main wings -->
      <path d="M24 16 L4 22 L4 24.5 L24 20 L44 24.5 L44 22 Z" fill="${color}" stroke="${outline}" stroke-width="0.8"/>
      <!-- horizontal stabilizer -->
      <path d="M24 34 L12 37 L12 38.5 L24 36 L36 38.5 L36 37 Z" fill="${color}" stroke="${outline}" stroke-width="0.8"/>
      <!-- vertical stabilizer -->
      <path d="M24 34 L24 38 L26 40 L26 36 Z" fill="${outline}" opacity="0.5"/>
      <!-- nose highlight -->
      <ellipse cx="24" cy="7" rx="1.5" ry="2.5" fill="white" opacity="0.15"/>
    </svg>`

  const helicopterSvg = `
    <svg width="48" height="48" viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg">
      <!-- fuselage -->
      <ellipse cx="24" cy="22" rx="5" ry="8" fill="${color}" stroke="${outline}" stroke-width="0.8"/>
      <!-- tail boom -->
      <path d="M22.5 30 L22.5 42 L25.5 42 L25.5 30 Z" fill="${color}" stroke="${outline}" stroke-width="0.6"/>
      <!-- tail rotor -->
      <path d="M24 42 L20 40 L20 41 L24 43 L28 41 L28 40 Z" fill="${color}" stroke="${outline}" stroke-width="0.6"/>
      <!-- main rotor disc -->
      <ellipse cx="24" cy="18" rx="18" ry="2.5" fill="${color}" opacity="0.3" stroke="${outline}" stroke-width="0.6" stroke-dasharray="3 2"/>
      <!-- rotor hub -->
      <circle cx="24" cy="18" r="2" fill="${outline}" opacity="0.6"/>
      <!-- skids -->
      <path d="M18 28 L18 30 L16 30" stroke="${outline}" stroke-width="1" fill="none"/>
      <path d="M30 28 L30 30 L32 30" stroke="${outline}" stroke-width="1" fill="none"/>
      <!-- cockpit glass -->
      <ellipse cx="24" cy="17" rx="3" ry="3.5" fill="white" opacity="0.1"/>
    </svg>`

  return L.divIcon({
    className: '',
    html: `<div style="
      transform: rotate(${hdgDeg}deg);
      transform-origin: center;
      width: 48px; height: 48px;
      display: flex; align-items: center; justify-content: center;
      filter: drop-shadow(0 1px 3px rgba(0,0,0,0.5));
    ">${isHelicopter ? helicopterSvg : fixedWingSvg}</div>`,
    iconSize: [48, 48],
    iconAnchor: [24, 24],
    popupAnchor: [0, -24],
  })
}

function MapFollower({ lat, lon, ctrOnAircraft }) {
  const map = useMap()
  useEffect(() => {
    if (ctrOnAircraft) map.setView([lat, lon], map.getZoom(), { animate: true })
  }, [lat, lon, ctrOnAircraft, map])
  return null
}

function MapOverlayControls() {
  const { ctrOnAircraft, clearTrack } = useSimStore()
  const set = useSimStore.setState

  const btnStyle = {
    width: 44, height: 44, minHeight: 44,
    background: 'rgba(13,17,23,0.85)', border: '1px solid #30363d',
    borderRadius: 4, color: '#8b949e', fontSize: 11, fontFamily: 'monospace',
    fontWeight: 700, cursor: 'pointer', display: 'flex', alignItems: 'center',
    justifyContent: 'center', touchAction: 'manipulation',
  }

  const handle = (fn) => (e) => { e.preventDefault(); fn() }

  return (
    <div style={{
      position: 'absolute', top: 10, right: 10, zIndex: 1000,
      display: 'flex', flexDirection: 'column', gap: 4,
    }}>
      <button
        style={{ ...btnStyle, color: ctrOnAircraft ? '#3fb950' : '#8b949e' }}
        onClick={handle(() => set({ ctrOnAircraft: !ctrOnAircraft }))}
        onTouchEnd={handle(() => set({ ctrOnAircraft: !ctrOnAircraft }))}
      >CTR A/C</button>
      <button
        style={btnStyle}
        onClick={handle(() => clearTrack())}
        onTouchEnd={handle(() => clearTrack())}
      >CLR TRACK</button>
    </div>
  )
}

export default function MapView() {
  const { fdm, simState, track, ctrOnAircraft } = useSimStore()

  const icon = useMemo(
    () => createAircraftIcon(fdm.hdgTrueDeg, simState, fdm.isHelicopter),
    [fdm.hdgTrueDeg, simState, fdm.isHelicopter]
  )

  return (
    <div style={{ width: '100%', height: '100%', position: 'relative' }}>
      <MapContainer
        center={[fdm.lat, fdm.lon]}
        zoom={10}
        zoomControl={false}
        style={{ width: '100%', height: '100%' }}
      >
        <TileLayer
          url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
          attribution="&copy; OpenStreetMap contributors"
          maxZoom={18}
        />
        <MapFollower lat={fdm.lat} lon={fdm.lon} ctrOnAircraft={ctrOnAircraft} />
        {track.length > 1 && (
          <Polyline positions={track} color="#2563eb" weight={3} opacity={0.9} />
        )}
        <Marker position={[fdm.lat, fdm.lon]} icon={icon} />
      </MapContainer>
      <MapOverlayControls />
    </div>
  )
}
