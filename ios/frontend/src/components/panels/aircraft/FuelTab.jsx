import { useSimStore } from '../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { PanelRow, SectionHeader } from '../PanelUtils'

export default function FuelTab() {
  const { fuel, fuelConfig, aircraftId } = useSimStore(useShallow(s => ({
    fuel: s.fuel, fuelConfig: s.fuelConfig, aircraftId: s.aircraftId,
  })))

  return (
    <div>
      <SectionHeader title={`FUEL — ${fuelConfig.fuelType}`} />
      {fuel.tanks.length === 0 && (
        <div style={{ color: '#64748b', fontSize: 12 }}>No fuel data</div>
      )}
      {fuel.tanks.map((tank, i) => {
        const cfgTank = fuelConfig.tanks?.[i] || {}
        const name = cfgTank.name || `Tank ${i + 1}`
        const unit = fuelConfig.displayUnit || 'L'
        const displayQty = unit === 'L' ? tank.liters : tank.massKg
        const displayCap = unit === 'L' ? (cfgTank.capacity_liters || 0) : (cfgTank.capacity_kg || 0)
        return (
          <div key={i} style={{ marginBottom: 12 }}>
            <div style={{
              display: 'flex', justifyContent: 'space-between', fontSize: 12,
              fontFamily: 'monospace', marginBottom: 4,
            }}>
              <span style={{ color: '#64748b' }}>{name}</span>
              <span style={{ color: '#e2e8f0' }}>
                {displayQty?.toFixed(1) ?? '--'} {unit}  {((tank.pct ?? 0) * 100).toFixed(0)}%
              </span>
            </div>
            <div style={{ height: 10, background: '#1c2333', borderRadius: 2, overflow: 'hidden' }}>
              <div style={{
                height: '100%', borderRadius: 2,
                width: `${(tank.pct ?? 0) * 100}%`,
                background: (tank.pct ?? 0) < 0.2 ? '#ff3b30' : '#00ff88',
                transition: 'width 0.3s',
              }} />
            </div>
          </div>
        )
      })}
      {(() => {
        const unit = fuelConfig.displayUnit || 'L'
        const totalDisplay = unit === 'L' ? fuel.totalFuelLiters : fuel.totalFuelKg
        return <PanelRow label="Total Fuel" value={`${totalDisplay?.toFixed(1) ?? '--'}`} unit={unit} />
      })()}
      <PanelRow label="Aircraft" value={aircraftId.toUpperCase()} />

      {/* Weight & Balance — future */}
    </div>
  )
}
