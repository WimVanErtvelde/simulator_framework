import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import { useShallow } from 'zustand/react/shallow'

// Tabs row + inline Accept/Discard + dirty indicator. Replaces the former
// WeatherV2Header — one less horizontal strip in the panel.

const TABS = [
  { id: 'global',     label: 'GLOBAL' },
  { id: 'patches',    label: 'PATCHES' },
  { id: 'microburst', label: 'MICROBURST' },
]

export default function WeatherV2Tabs() {
  const { activeTab, setActiveTab, draft, serverState, accept, discard } = useWeatherV2Store(
    useShallow(s => ({
      activeTab:    s.activeTab,
      setActiveTab: s.setActiveTab,
      draft:        s.draft,
      serverState:  s.serverState,
      accept:       s.accept,
      discard:      s.discard,
    }))
  )
  // Reactive dirty check — selector depends on draft+serverState so Zustand
  // re-renders this component whenever either changes.
  const isDirty = JSON.stringify(draft) !== JSON.stringify(serverState)

  return (
    <div style={{
      display: 'flex', alignItems: 'center', justifyContent: 'space-between',
      borderBottom: '1px solid #1e293b', marginBottom: 12, padding: '0 4px',
    }}>
      {/* Left — tab buttons */}
      <div style={{ display: 'flex' }}>
        {TABS.map(tab => {
          const active = activeTab === tab.id
          return (
            <button
              key={tab.id}
              type="button"
              onClick={() => setActiveTab(tab.id)}
              onTouchEnd={(e) => { e.preventDefault(); setActiveTab(tab.id) }}
              style={{
                padding: '10px 16px',
                background: 'transparent', border: 'none',
                borderBottom: `2px solid ${active ? '#39d0d8' : 'transparent'}`,
                color: active ? '#39d0d8' : '#64748b',
                fontFamily: 'monospace',
                fontSize: active ? 13 : 12,
                fontWeight: active ? 700 : 500,
                letterSpacing: 1, textTransform: 'uppercase',
                cursor: 'pointer', touchAction: 'manipulation',
                transition: 'color 80ms, border-color 80ms',
              }}
              onMouseEnter={(e) => { if (!active) e.currentTarget.style.color = '#94a3b8' }}
              onMouseLeave={(e) => { if (!active) e.currentTarget.style.color = '#64748b' }}
            >{tab.label}</button>
          )
        })}
      </div>

      {/* Right — Unsaved indicator + Discard + Accept */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, paddingRight: 4 }}>
        {isDirty && (
          <span style={{
            fontSize: 10, color: '#bc4fcb', fontFamily: 'monospace',
            textTransform: 'uppercase', letterSpacing: 1,
          }}>Unsaved</span>
        )}
        <ToolBtn disabled={!isDirty} onClick={discard} tone="neutral">Discard</ToolBtn>
        <ToolBtn disabled={!isDirty} onClick={accept}  tone="primary">Accept</ToolBtn>
      </div>
    </div>
  )
}

function ToolBtn({ disabled, onClick, tone, children }) {
  const isPrimary = tone === 'primary'
  return (
    <button
      type="button"
      disabled={disabled}
      onClick={onClick}
      onTouchEnd={(e) => { e.preventDefault(); if (!disabled) onClick?.() }}
      style={{
        height: 28, padding: `0 ${isPrimary ? 14 : 10}px`,
        background:  disabled ? '#111827' : (isPrimary ? 'rgba(188, 79, 203, 0.10)' : '#111827'),
        border: `1px solid ${disabled ? '#1e293b' : (isPrimary ? '#bc4fcb' : '#1e293b')}`,
        borderRadius: 3,
        color: disabled ? '#334155' : (isPrimary ? '#bc4fcb' : '#94a3b8'),
        fontFamily: 'monospace', fontSize: 11, fontWeight: 700,
        letterSpacing: 1, textTransform: 'uppercase',
        cursor: disabled ? 'not-allowed' : 'pointer',
        opacity: disabled ? 0.5 : 1,
        touchAction: 'manipulation',
      }}
    >{children}</button>
  )
}
