import { useSimStore } from '../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { SectionHeader } from '../PanelUtils'

function WarningFlag({ active, label }) {
  return active ? (
    <span style={{
      fontSize: 10, fontWeight: 700, fontFamily: 'monospace',
      padding: '1px 5px', borderRadius: 2, marginLeft: 6,
      background: '#ff3b30', color: '#fff', letterSpacing: 0.5,
    }}>{label}</span>
  ) : null
}

function GaugeRow({ label, value, unit, warn, max }) {
  const pct = max > 0 ? Math.min(value / max, 1) : 0
  const barColor = warn ? '#ff3b30' : pct > 0.9 ? '#f59e0b' : '#00ff88'
  return (
    <div style={{ marginBottom: 4 }}>
      <div style={{
        display: 'flex', justifyContent: 'space-between', fontSize: 12,
        fontFamily: 'monospace', marginBottom: 2,
      }}>
        <span style={{ color: '#64748b' }}>{label}</span>
        <span style={{ color: warn ? '#ff3b30' : '#e2e8f0' }}>
          {typeof value === 'number' ? value.toFixed(1) : '--'} {unit}
        </span>
      </div>
      {max > 0 && (
        <div style={{ height: 6, background: '#1c2333', borderRadius: 2, overflow: 'hidden' }}>
          <div style={{
            height: '100%', borderRadius: 2, width: `${pct * 100}%`,
            background: barColor, transition: 'width 0.15s',
          }} />
        </div>
      )}
    </div>
  )
}

function PistonEngineDisplay({ idx, engines, config, fuel }) {
  const running = engines.engineRunning[idx]
  const cfg = config?.engines?.[idx] || {}
  const ffLph = fuel.engineFuelFlowLph?.[idx] ?? 0
  return (
    <>
      <div style={{
        display: 'flex', alignItems: 'center', gap: 8, marginBottom: 6,
        fontSize: 12, fontFamily: 'monospace',
      }}>
        <span style={{
          width: 8, height: 8, borderRadius: '50%',
          background: running ? '#00ff88' : '#ef4444',
        }} />
        <span style={{ color: '#e2e8f0', fontWeight: 700 }}>
          ENG {idx + 1} {running ? 'RUNNING' : 'OFF'}
        </span>
        {engines.engineFailed[idx] && <WarningFlag active label="FAIL" />}
        {engines.starterEngaged[idx] && (
          <span style={{ fontSize: 10, color: '#39d0d8', fontFamily: 'monospace' }}>STR</span>
        )}
      </div>
      <GaugeRow label="RPM" value={engines.rpm[idx]} unit=""
        max={cfg.rpm_max || 2700} warn={false} />
      <GaugeRow label="MAP" value={engines.manifoldPressureInhg[idx]} unit="inHg"
        max={cfg.manifold_pressure_max_inhg || 30} warn={false} />
      <GaugeRow label="EGT" value={engines.egtDegc[idx]} unit={'\u00B0C'}
        max={cfg.egt_max_degc || 900} warn={engines.highEgtWarning[idx]} />
      <GaugeRow label="CHT" value={engines.chtDegc[idx]} unit={'\u00B0C'}
        max={cfg.cht_max_degc || 240} warn={engines.highChtWarning[idx]} />
      <GaugeRow label="Oil P" value={engines.oilPressurePsi[idx]} unit="psi"
        max={cfg.oil_pressure_normal_psi || 80}
        warn={engines.lowOilPressureWarning[idx]} />
      <GaugeRow label="Oil T" value={engines.oilTempDegc[idx]} unit={'\u00B0C'}
        max={cfg.oil_temp_max_degc || 120} warn={false} />
      <GaugeRow label="FF" value={ffLph} unit="L/h"
        max={45} warn={false} />
    </>
  )
}

function TurboshaftEngineDisplay({ idx, engines, config, fuel }) {
  const running = engines.engineRunning[idx]
  const cfg = config?.engines?.[idx] || {}
  const ffLph = fuel.engineFuelFlowLph?.[idx] ?? 0
  return (
    <>
      <div style={{
        display: 'flex', alignItems: 'center', gap: 8, marginBottom: 6,
        fontSize: 12, fontFamily: 'monospace',
      }}>
        <span style={{
          width: 8, height: 8, borderRadius: '50%',
          background: running ? '#00ff88' : '#ef4444',
        }} />
        <span style={{ color: '#e2e8f0', fontWeight: 700 }}>
          ENG {idx + 1} {running ? 'RUNNING' : 'OFF'}
        </span>
        {engines.engineFailed[idx] && <WarningFlag active label="FAIL" />}
        {engines.starterEngaged[idx] && (
          <span style={{ fontSize: 10, color: '#39d0d8', fontFamily: 'monospace' }}>STR</span>
        )}
      </div>
      <GaugeRow label="N1" value={engines.n1Pct[idx]} unit="%"
        max={cfg.n1_max_pct || 100} warn={false} />
      <GaugeRow label="N2" value={engines.n2Pct[idx]} unit="%"
        max={cfg.n2_max_pct || 100} warn={false} />
      <GaugeRow label="TGT" value={engines.tgtDegc[idx]} unit={'\u00B0C'}
        max={cfg.tgt_max_degc || 810} warn={engines.highEgtWarning[idx]} />
      <GaugeRow label="TRQ" value={engines.torquePct[idx]} unit="%"
        max={cfg.torque_max_pct || 100} warn={false} />
      <GaugeRow label="Oil P" value={engines.oilPressurePsi[idx]} unit="psi"
        max={80} warn={engines.lowOilPressureWarning[idx]} />
      <GaugeRow label="Oil T" value={engines.oilTempDegc[idx]} unit={'\u00B0C'}
        max={120} warn={false} />
      <GaugeRow label="FF" value={ffLph} unit="L/h"
        max={180} warn={false} />
    </>
  )
}

export default function EnginesTab() {
  const { engines, engineConfig, fuel } = useSimStore(useShallow(s => ({
    engines: s.engines, engineConfig: s.engineConfig, fuel: s.fuel,
  })))

  if (engines.engineCount === 0) {
    return <div style={{ color: '#64748b', fontSize: 12, fontFamily: 'monospace' }}>No engine data</div>
  }

  return (
    <div>
      <SectionHeader title="ENGINES" />
      {Array.from({ length: engines.engineCount }).map((_, i) => {
        const type = engineConfig.engines?.[i]?.type || 'piston'
        return (
          <div key={i} style={{ marginBottom: i < engines.engineCount - 1 ? 12 : 0 }}>
            {type === 'turboshaft' ? (
              <TurboshaftEngineDisplay idx={i} engines={engines} config={engineConfig} fuel={fuel} />
            ) : (
              <PistonEngineDisplay idx={i} engines={engines} config={engineConfig} fuel={fuel} />
            )}
          </div>
        )
      })}
    </div>
  )
}
