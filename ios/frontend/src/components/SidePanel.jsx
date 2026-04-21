import { useSimStore } from '../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import PositionPanel from './panels/PositionPanel'
import AircraftPanel from './panels/AircraftPanel'
import FailuresPanel from './panels/FailuresPanel'
import WeatherPanelV2 from './panels/WeatherPanelV2'
import TimePanel from './panels/TimePanel'
import ScenariosPanel from './panels/ScenariosPanel'
import NodesPanel from './panels/NodesPanel'
import InspectorPanel from './panels/InspectorPanel'
import SessionPanel from './panels/SessionPanel'

const TABS = {
  position: { title: 'POSITION', component: PositionPanel },
  aircraft: { title: 'AIRCRAFT', component: AircraftPanel },
  failures: { title: 'FAILURES', component: FailuresPanel },
  weather: { title: 'WEATHER', component: WeatherPanelV2 },
  time: { title: 'TIME', component: TimePanel },
  scenarios: { title: 'SCENARIOS', component: ScenariosPanel },
  nodes: { title: 'NODES', component: NodesPanel },
  inspector: { title: 'STATE INSPECTOR', component: InspectorPanel },
  session: { title: 'SESSION', component: SessionPanel },
}

export default function SidePanel() {
  const { activeTab } = useSimStore(useShallow(s => ({ activeTab: s.activeTab })))
  const tabInfo = TABS[activeTab]

  if (!tabInfo) return null

  return (
    <div style={{
      width: '100%',
      height: '100%',
      background: '#0a0e17',
      display: 'flex',
      flexDirection: 'column',
      overflow: 'hidden',
    }}>
      <PanelHeader title={tabInfo.title} />
      <PanelContent><tabInfo.component /></PanelContent>
    </div>
  )
}

function PanelHeader({ title }) {
  return (
    <div style={{
      height: 44, display: 'flex', alignItems: 'center', padding: '0 16px',
      background: '#0d1117', borderBottom: '1px solid #1e293b',
      flexShrink: 0,
    }}>
      <span style={{ fontSize: 12, fontWeight: 700, textTransform: 'uppercase', letterSpacing: 2, color: '#39d0d8' }}>{title}</span>
    </div>
  )
}

function PanelContent({ children }) {
  return (
    <div style={{ flex: 1, minHeight: 0, overflowY: 'auto', padding: 16, background: '#0a0e17' }}>
      {children}
    </div>
  )
}
