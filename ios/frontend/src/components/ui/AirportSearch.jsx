import { useState, useRef, useCallback } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import KeyboardPopup from './KeyboardPopup'

const FT2M = 0.3048

const inputBase = {
  width: '100%', height: 44, padding: '8px 12px',
  background: '#1c2333', border: '1px solid #1e293b', borderRadius: 3,
  color: '#e2e8f0', fontFamily: 'monospace', fontSize: 13,
  WebkitAppearance: 'none', outline: 'none',
}

// Shared airport search dropdown.
// onSelect is called with the full airport object from search results
// (icao, name, city, country, iata, arp_lat_deg, arp_lon_deg, elevation_m,
//  transition_altitude_ft, runways[]) or null when cleared.
export default function AirportSearch({ value, onSelect, placeholder = 'ICAO or name' }) {
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
        onClick={() => {
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
