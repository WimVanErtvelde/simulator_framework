import { useState, useEffect, useRef, useCallback } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { SectionHeader, FullWidthBtn } from './PanelUtils'

const ATA_CHAPTERS = [
  { code: 21, name: 'AIR COND & PRESS' },
  { code: 22, name: 'AUTO FLIGHT' },
  { code: 24, name: 'ELECTRICAL POWER' },
  { code: 26, name: 'FIRE PROTECTION' },
  { code: 27, name: 'FLIGHT CONTROLS' },
  { code: 28, name: 'FUEL' },
  { code: 29, name: 'HYDRAULIC POWER' },
  { code: 30, name: 'ICE & RAIN PROT' },
  { code: 32, name: 'LANDING GEAR' },
  { code: 34, name: 'NAVIGATION' },
  { code: 61, name: 'PROPELLERS' },
  { code: 71, name: 'POWER PLANT' },
]

// ── Navaid search sub-component for world_navaid failures ────────────────────

function NavaidSearch({ onInject }) {
  const [query, setQuery] = useState('')
  const [typeFilter, setTypeFilter] = useState('ALL')
  const [results, setResults] = useState([])
  const [selected, setSelected] = useState(null)
  const debounceRef = useRef(null)

  const doSearch = useCallback(async (q, types) => {
    if (!q || q.length < 1) { setResults([]); return }
    const params = new URLSearchParams({ q, limit: '20' })
    if (types !== 'ALL') params.set('types', types)
    try {
      const res = await fetch(`/api/navaids/search?${params}`)
      if (res.ok) setResults(await res.json())
    } catch { /* ignore */ }
  }, [])

  useEffect(() => {
    clearTimeout(debounceRef.current)
    debounceRef.current = setTimeout(() => doSearch(query, typeFilter), 300)
    return () => clearTimeout(debounceRef.current)
  }, [query, typeFilter, doSearch])

  const handleInject = () => {
    if (!selected) return
    onInject(selected.ident)
    setSelected(null)
    setQuery('')
    setResults([])
  }

  const inputStyle = {
    width: '100%', height: 32, background: '#0a0e17', border: '1px solid #1e293b',
    borderRadius: 3, color: '#e2e8f0', fontSize: 12, fontFamily: 'monospace',
    padding: '0 8px', outline: 'none',
  }

  return (
    <div style={{ padding: '8px 0' }}>
      <div style={{ display: 'flex', gap: 6, marginBottom: 6 }}>
        <input
          type="text"
          placeholder="Search navaids..."
          value={query}
          onChange={(e) => { setQuery(e.target.value); setSelected(null) }}
          style={{ ...inputStyle, flex: 1 }}
        />
        <select
          value={typeFilter}
          onChange={(e) => setTypeFilter(e.target.value)}
          style={{ ...inputStyle, width: 72, cursor: 'pointer' }}
        >
          <option value="ALL">ALL</option>
          <option value="VOR">VOR</option>
          <option value="ILS">ILS</option>
          <option value="NDB">NDB</option>
        </select>
      </div>

      {results.length > 0 && (
        <div style={{
          maxHeight: 220, overflowY: 'auto', border: '1px solid #1e293b',
          borderRadius: 3, background: '#111827',
        }}>
          {results.map((nav, i) => {
            const isSelected = selected?.ident === nav.ident && selected?.type === nav.type
              && selected?.freq_mhz === nav.freq_mhz
            return (
              <button
                key={`${nav.ident}-${nav.type}-${nav.freq_mhz}-${i}`}
                onClick={() => setSelected(nav)}
                style={{
                  width: '100%', display: 'flex', alignItems: 'center', gap: 8,
                  padding: '6px 10px', border: 'none', borderBottom: '1px solid #1e293b',
                  background: isSelected ? 'rgba(0, 255, 136, 0.1)' : 'transparent',
                  color: '#e2e8f0', fontSize: 11, fontFamily: 'monospace',
                  cursor: 'pointer', textAlign: 'left',
                }}
              >
                <span style={{ color: '#00ff88', minWidth: 42, fontWeight: 700 }}>{nav.ident}</span>
                <span style={{ color: '#64748b', flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{nav.name}</span>
                <span style={{ color: '#39d0d8', minWidth: 30 }}>{nav.type}</span>
                <span style={{ color: '#94a3b8', minWidth: 50, textAlign: 'right' }}>
                  {nav.type === 'NDB' ? `${(nav.freq_mhz * 1000).toFixed(0)} kHz` : `${nav.freq_mhz.toFixed(2)}`}
                </span>
                <span style={{ color: '#475569', minWidth: 40, textAlign: 'right' }}>{nav.range_nm}nm</span>
              </button>
            )
          })}
        </div>
      )}

      <button
        onClick={handleInject}
        disabled={!selected}
        style={{
          width: '100%', height: 36, marginTop: 6,
          background: selected ? 'rgba(255, 59, 48, 0.15)' : 'rgba(255, 59, 48, 0.04)',
          border: `1px solid ${selected ? '#ff3b30' : '#334155'}`,
          borderRadius: 3, color: selected ? '#ff3b30' : '#334155',
          fontSize: 12, fontFamily: 'monospace', fontWeight: 700,
          cursor: selected ? 'pointer' : 'default',
        }}
      >
        {selected ? `INJECT: ${selected.ident} (${selected.type})` : 'SELECT A STATION'}
      </button>
    </div>
  )
}

// ── Failure row ──────────────────────────────────────────────────────────────

function FailureRow({ failure, isActive, onInject, onClear }) {
  const isNavaid = failure.id.startsWith('world_navaid')
  const [showSearch, setShowSearch] = useState(false)

  const handleInjectNavaid = (stationId) => {
    onInject(failure.id, JSON.stringify({ station_id: stationId }))
    setShowSearch(false)
  }

  return (
    <div style={{ borderBottom: '1px solid #1e293b' }}>
      <div style={{
        display: 'flex', alignItems: 'center', padding: '6px 12px 6px 24px',
        background: isActive ? 'rgba(255, 59, 48, 0.06)' : '#0a0e17',
        gap: 8, minHeight: 36,
      }}>
        <span style={{
          color: isActive ? '#ff3b30' : '#94a3b8',
          fontSize: 12, fontFamily: 'monospace', flex: 1,
        }}>
          {failure.display_name}
        </span>

        {isActive ? (
          <button
            onClick={() => onClear(failure.id)}
            style={{
              height: 28, padding: '0 12px', background: 'rgba(255, 59, 48, 0.1)',
              border: '1px solid #ff3b30', borderRadius: 3,
              color: '#ff3b30', fontSize: 11, fontFamily: 'monospace', fontWeight: 700,
              cursor: 'pointer',
            }}
          >CLR</button>
        ) : isNavaid ? (
          <button
            onClick={() => setShowSearch(!showSearch)}
            style={{
              height: 28, padding: '0 12px', background: 'rgba(57, 208, 216, 0.08)',
              border: '1px solid #39d0d8', borderRadius: 3,
              color: '#39d0d8', fontSize: 11, fontFamily: 'monospace', fontWeight: 700,
              cursor: 'pointer',
            }}
          >{showSearch ? 'CANCEL' : 'SEARCH'}</button>
        ) : (
          <button
            onClick={() => onInject(failure.id)}
            style={{
              height: 28, padding: '0 12px', background: 'rgba(255, 165, 0, 0.08)',
              border: '1px solid #d97706', borderRadius: 3,
              color: '#d97706', fontSize: 11, fontFamily: 'monospace', fontWeight: 700,
              cursor: 'pointer',
            }}
          >INJECT</button>
        )}
      </div>

      {isNavaid && showSearch && !isActive && (
        <div style={{ padding: '0 12px 8px 24px', background: '#0a0e17' }}>
          <NavaidSearch onInject={handleInjectNavaid} />
        </div>
      )}
    </div>
  )
}

// ── Main panel ───────────────────────────────────────────────────────────────

export default function FailuresPanel() {
  const { activeFailures, activeFailureIds, failuresCatalog,
          injectFailure, clearFailure, clearAllFailures } = useSimStore()
  const [expanded, setExpanded] = useState({})
  const [worldExpanded, setWorldExpanded] = useState(false)

  const toggle = (code) => setExpanded((prev) => ({ ...prev, [code]: !prev[code] }))

  // Group catalog by ATA chapter
  const byAta = {}
  const worldFailures = []
  for (const f of failuresCatalog) {
    if (f.ata == null) {
      worldFailures.push(f)
    } else {
      if (!byAta[f.ata]) byAta[f.ata] = []
      byAta[f.ata].push(f)
    }
  }

  const activeSet = new Set(activeFailureIds)

  return (
    <div>
      <SectionHeader title="ACTIVE FAILURES" />
      {activeFailures === 0 ? (
        <div style={{ color: '#64748b', fontSize: 12, padding: '8px 0' }}>No active failures</div>
      ) : (
        <>
          <div style={{ color: '#ff3b30', fontSize: 14, fontWeight: 700, fontFamily: 'monospace', padding: '8px 0' }}>
            {activeFailures} active failure{activeFailures !== 1 ? 's' : ''}
          </div>
          <FullWidthBtn
            label="CLEAR ALL"
            style={{ background: 'rgba(255, 59, 48, 0.08)', color: '#ff3b30', borderColor: '#ff3b30', height: 36 }}
            onClick={clearAllFailures}
          />
        </>
      )}

      <SectionHeader title="ATA SYSTEMS" />
      {ATA_CHAPTERS.map((ch) => {
        const failures = byAta[ch.code] || []
        const activeCount = failures.filter(f => activeSet.has(f.id)).length
        return (
          <div key={ch.code}>
            <button
              onClick={() => toggle(ch.code)}
              onTouchEnd={(e) => { e.preventDefault(); toggle(ch.code) }}
              style={{
                width: '100%', height: 44, display: 'flex', alignItems: 'center',
                padding: '0 12px',
                background: '#111827', border: '1px solid #1e293b', borderRadius: 3,
                fontSize: 13, fontFamily: 'monospace',
                cursor: 'pointer', touchAction: 'manipulation',
                marginBottom: 2,
              }}
            >
              <span style={{ color: '#39d0d8', minWidth: 50 }}>ATA {ch.code}</span>
              <span style={{ color: '#64748b' }}>{ch.name}</span>
              {activeCount > 0 && (
                <span style={{ color: '#ff3b30', marginLeft: 8, fontSize: 11 }}>
                  [{activeCount}]
                </span>
              )}
              <span style={{ color: failures.length > 0 ? '#475569' : '#1e293b', marginLeft: 'auto', fontSize: 11, marginRight: 8 }}>
                {failures.length > 0 ? failures.length : ''}
              </span>
              <span style={{ color: '#334155' }}>{expanded[ch.code] ? '\u25BC' : '\u25B6'}</span>
            </button>
            {expanded[ch.code] && (
              <div>
                {failures.length === 0 ? (
                  <div style={{ padding: '8px 12px 8px 24px', background: '#0a0e17', color: '#334155', fontSize: 12 }}>
                    No failures defined
                  </div>
                ) : (
                  failures.map(f => (
                    <FailureRow
                      key={f.id}
                      failure={f}
                      isActive={activeSet.has(f.id)}
                      onInject={injectFailure}
                      onClear={clearFailure}
                    />
                  ))
                )}
              </div>
            )}
          </div>
        )
      })}

      {worldFailures.length > 0 && (
        <>
          <SectionHeader title="WORLD" />
          <div>
            <button
              onClick={() => setWorldExpanded(!worldExpanded)}
              onTouchEnd={(e) => { e.preventDefault(); setWorldExpanded(!worldExpanded) }}
              style={{
                width: '100%', height: 44, display: 'flex', alignItems: 'center',
                padding: '0 12px',
                background: '#111827', border: '1px solid #1e293b', borderRadius: 3,
                fontSize: 13, fontFamily: 'monospace',
                cursor: 'pointer', touchAction: 'manipulation',
              }}
            >
              <span style={{ color: '#39d0d8', minWidth: 50 }}>ENV</span>
              <span style={{ color: '#64748b' }}>ENVIRONMENT</span>
              <span style={{ color: '#475569', marginLeft: 'auto', fontSize: 11, marginRight: 8 }}>{worldFailures.length}</span>
              <span style={{ color: '#334155' }}>{worldExpanded ? '\u25BC' : '\u25B6'}</span>
            </button>
            {worldExpanded && worldFailures.map(f => (
              <FailureRow
                key={f.id}
                failure={f}
                isActive={activeSet.has(f.id)}
                onInject={injectFailure}
                onClear={clearFailure}
              />
            ))}
          </div>
        </>
      )}
    </div>
  )
}
