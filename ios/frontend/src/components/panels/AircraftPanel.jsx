import { useState } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import FuelTab from './aircraft/FuelTab'
import ElectricalTab from './aircraft/ElectricalTab'
import EnginesTab from './aircraft/EnginesTab'
import RadiosTab from './aircraft/RadiosTab'

const TAB_DEFS = [
  { id: 'fuel',       label: 'FUEL & W/B',  configKey: 'fuelConfig',       component: FuelTab },
  { id: 'electrical', label: 'ELECTRICAL',   configKey: 'electricalConfig', component: ElectricalTab },
  { id: 'engines',    label: 'ENGINES',      configKey: 'engineConfig',     component: EnginesTab },
  { id: 'radios',     label: 'RADIOS',       configKey: 'avionicsConfig',   component: RadiosTab },
]

export default function AircraftPanel() {
  const configs = useSimStore(useShallow(s => ({
    fuelConfig: s.fuelConfig,
    electricalConfig: s.electricalConfig,
    engineConfig: s.engineConfig,
    avionicsConfig: s.avionicsConfig,
  })))

  const visibleTabs = TAB_DEFS.filter(t => {
    const cfg = configs[t.configKey]
    return cfg != null && (typeof cfg !== 'object' || Object.keys(cfg).length > 0)
  })

  const [activeTab, setActiveTab] = useState(visibleTabs[0]?.id || 'fuel')
  const current = visibleTabs.find(t => t.id === activeTab) || visibleTabs[0]

  return (
    <div>
      {/* Sub-tab bar */}
      <div style={{
        display: 'flex', gap: 0, borderBottom: '1px solid #1e293b', marginBottom: 12,
      }}>
        {visibleTabs.map(tab => {
          const active = tab.id === (current?.id)
          return (
            <button
              key={tab.id}
              onClick={() => setActiveTab(tab.id)}
              style={{
                padding: '8px 16px', fontSize: 10, fontWeight: 700,
                letterSpacing: 1, fontFamily: 'monospace', textTransform: 'uppercase',
                color: active ? '#f59e0b' : '#64748b',
                background: 'transparent', border: 'none',
                borderBottom: `2px solid ${active ? '#f59e0b' : 'transparent'}`,
                cursor: 'pointer', transition: 'color 0.15s',
              }}
              onMouseEnter={e => { if (!active) e.currentTarget.style.color = '#94a3b8' }}
              onMouseLeave={e => { if (!active) e.currentTarget.style.color = '#64748b' }}
            >
              {tab.label}
            </button>
          )
        })}
      </div>

      {/* Active tab content */}
      {current && <current.component />}
    </div>
  )
}
