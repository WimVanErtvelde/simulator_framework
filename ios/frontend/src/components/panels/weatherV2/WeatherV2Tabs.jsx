import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import { useShallow } from 'zustand/react/shallow'

const TABS = [
  { id: 'global',     label: 'GLOBAL' },
  { id: 'patches',    label: 'PATCHES' },
  { id: 'microburst', label: 'MICROBURST' },
]

export default function WeatherV2Tabs() {
  const { activeTab, setActiveTab } = useWeatherV2Store(useShallow(s => ({
    activeTab:    s.activeTab,
    setActiveTab: s.setActiveTab,
  })))

  return (
    <div style={{
      display: 'flex', gap: 0, borderBottom: '1px solid #1e293b', marginBottom: 12,
    }}>
      {TABS.map(tab => {
        const active = activeTab === tab.id
        return (
          <button
            key={tab.id}
            onClick={() => setActiveTab(tab.id)}
            onTouchEnd={(e) => { e.preventDefault(); setActiveTab(tab.id) }}
            style={{
              padding: '8px 16px', fontSize: 10, fontWeight: 700,
              letterSpacing: 1, fontFamily: 'monospace', textTransform: 'uppercase',
              color: active ? '#39d0d8' : '#64748b',
              background: 'transparent', border: 'none',
              borderBottom: `2px solid ${active ? '#39d0d8' : 'transparent'}`,
              cursor: 'pointer', transition: 'color 0.15s',
              touchAction: 'manipulation',
            }}
            onMouseEnter={e => { if (!active) e.currentTarget.style.color = '#94a3b8' }}
            onMouseLeave={e => { if (!active) e.currentTarget.style.color = '#64748b' }}
          >
            {tab.label}
          </button>
        )
      })}
    </div>
  )
}
