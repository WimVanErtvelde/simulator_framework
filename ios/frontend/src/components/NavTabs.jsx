import { useSimStore } from '../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'

const TABS = [
  { id: 'map',       icon: '◈',  label: 'MAP' },
  null, // divider
  { id: 'position',  icon: '⊕',  label: 'POS' },
  { id: 'aircraft',  icon: '▲',  label: 'A/C' },
  null, // divider
  { id: 'failures',  icon: '◉',  label: 'FAIL' },
  { id: 'weather',   icon: '☁',  label: 'WX' },
  { id: 'time',      icon: '◷',  label: 'TIME' },
  { id: 'scenarios', icon: '▤',  label: 'SCEN' },
  null, // divider
  { id: 'nodes',     icon: '⬡',  label: 'NODE' },
  { id: 'inspector', icon: '⟐',  label: 'DBG' },
  { id: 'session',   icon: '◌',  label: 'SESS' },
]

export default function NavTabs() {
  const { activeTab, setActiveTab, activeFailures, nodes } = useSimStore(useShallow(s => ({
    activeTab: s.activeTab, setActiveTab: s.setActiveTab,
    activeFailures: s.activeFailures, nodes: s.nodes,
  })))
  const hasLostNode = Object.values(nodes).some(n => n.status === 'LOST')

  return (
    <div style={{
      width: 72, height: '100%', background: '#0a0e17',
      borderRight: '1px solid #1e293b', display: 'flex', flexDirection: 'column',
      overflowY: 'auto',
    }}>
      {TABS.map((tab, i) => {
        if (!tab) {
          return <div key={`div-${i}`} style={{ height: 1, background: '#1e293b', margin: '2px 10px' }} />
        }

        const active = activeTab === tab.id
        return (
          <button
            key={tab.id}
            onClick={() => setActiveTab(tab.id)}
            onTouchEnd={(e) => { e.preventDefault(); setActiveTab(tab.id) }}
            style={{
              width: 72, height: 68, minHeight: 68,
              display: 'flex', flexDirection: 'column',
              alignItems: 'center', justifyContent: 'center', gap: 4,
              background: active ? '#111827' : 'transparent',
              border: 'none',
              borderLeft: `3px solid ${active ? '#00ff88' : 'transparent'}`,
              cursor: 'pointer', position: 'relative', padding: 0,
              touchAction: 'manipulation',
              transition: 'all 0.15s',
            }}
            onMouseEnter={(e) => { if (!active) e.currentTarget.style.background = 'rgba(255,255,255,0.04)' }}
            onMouseLeave={(e) => { if (!active) e.currentTarget.style.background = 'transparent' }}
          >
            <span style={{
              fontSize: 22, position: 'relative',
              color: active ? '#00ff88' : '#64748b',
            }}>
              {tab.icon}
              {/* Failures badge */}
              {tab.id === 'failures' && activeFailures > 0 && (
                <span style={{
                  position: 'absolute', top: -8, right: -14,
                  borderRadius: 8, padding: '1px 5px',
                  background: '#ff3b30', color: '#fff', fontSize: 10,
                  fontWeight: 700, lineHeight: '16px',
                }}>{activeFailures}</span>
              )}
              {/* Nodes lost dot */}
              {tab.id === 'nodes' && hasLostNode && (
                <span style={{
                  position: 'absolute', top: -4, right: -8,
                  width: 9, height: 9, borderRadius: '50%', background: '#ff3b30',
                }} />
              )}
            </span>
            <span style={{
              fontSize: 10, fontFamily: 'monospace', letterSpacing: '1px',
              textTransform: 'uppercase',
              color: active ? '#00ff88' : '#475569',
            }}>{tab.label}</span>
          </button>
        )
      })}
    </div>
  )
}
