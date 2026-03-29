import { useSimStore } from './store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import StatusStrip from './components/StatusStrip'
import NavTabs from './components/NavTabs'
import MapView from './components/MapView'
import SidePanel from './components/SidePanel'
import ActionBar from './components/ActionBar'

export default function App() {
  const { activeTab } = useSimStore(useShallow(s => ({ activeTab: s.activeTab })))

  return (
    <div style={{
      display: 'grid',
      gridTemplateRows: 'auto 1fr auto',
      gridTemplateColumns: '72px 1fr',
      height: '100vh',
      width: '100vw',
      overflow: 'hidden',
      background: '#0d1117',
      color: '#e6edf3',
      fontFamily: "'JetBrains Mono', 'Fira Code', 'SF Mono', monospace",
    }}>
      {/* Row 1: Status strip full width */}
      <div style={{ gridRow: 1, gridColumn: '1 / -1' }}>
        <StatusStrip />
      </div>

      {/* Row 2 col 1: Nav tabs */}
      <div style={{ gridRow: 2, gridColumn: 1 }}>
        <NavTabs />
      </div>

      {/* Row 2 col 2: Map OR panel — never both */}
      <div style={{ gridRow: 2, gridColumn: 2, position: 'relative', overflow: 'hidden' }}>
        {activeTab === 'map' ? (
          <MapView />
        ) : (
          <SidePanel />
        )}
      </div>

      {/* Row 3: Action bar full width */}
      <div style={{ gridRow: 3, gridColumn: '1 / -1' }}>
        <ActionBar />
      </div>
    </div>
  )
}
