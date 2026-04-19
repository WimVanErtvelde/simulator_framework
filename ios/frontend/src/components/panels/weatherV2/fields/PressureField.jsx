import { useState } from 'react'
import { useSimStore } from '../../../../store/useSimStore'
import { formatPressure, hpaToInHg, isaPressureAt } from '../weatherUnits'
import { M_TO_FT } from '../graph/mslScale'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider,
         COLOR_TEXT_MUTED } from './fieldStyles'
import { PillGroup } from './fieldCommon'
import OverridePill from './OverridePill'

const UNIT_OPTIONS = [{ id: 'hPa', label: 'hPa' }, { id: 'inHg', label: 'inHg' }]
const READOUT_VALUE_COLOR = '#94a3b8'
const HPA_TO_PA = 100.0
const PA_TO_HPA = 0.01

// Authors sea-level pressure. "At aircraft" readout shows the live
// atmosphere broadcast QNH at the aircraft's position. Station readout
// (dormant on Global) resolves authored SL pressure to station elevation.
export default function PressureField({
  value,
  onChange,
  showOverrideToggle  = false,
  overrideEnabled     = true,
  onToggleOverride    = () => {},
  showStationReadout  = false,
  stationIcao         = '',
  stationElevM        = 0,
}) {
  const [unit, setUnit] = useState('hPa')
  const qnhAtAircraftHpa = useSimStore(s => s.atmosphere?.qnhHpa)

  const disabled = !overrideEnabled
  const fmt = (hpa) => unit === 'inHg'
    ? `${hpaToInHg(hpa).toFixed(2)} inHg`
    : `${hpa.toFixed(1)} hPa`

  // Resolve authored SL hPa to station elevation via ISA pressure model.
  const stationHpa = showStationReadout && stationIcao
    ? isaPressureAt(value * HPA_TO_PA, stationElevM) * PA_TO_HPA
    : null

  return (
    <div style={{ ...fieldBox, opacity: disabled ? 0.4 : 1 }}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>Sea Level Press</span>
        {showOverrideToggle && (
          <OverridePill enabled={overrideEnabled} onToggle={onToggleOverride} />
        )}
        <span style={fieldValue}>{formatPressure(value, unit)}</span>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 110px', gap: 8, alignItems: 'center' }}>
        <input
          type="range"
          min={880} max={1075} step={0.1}
          value={value}
          disabled={disabled}
          onChange={(e) => onChange(Number(e.target.value))}
          style={slider}
        />
        <PillGroup options={UNIT_OPTIONS} value={unit} onChange={setUnit} />
      </div>

      {Number.isFinite(qnhAtAircraftHpa) && (
        <div style={{
          fontSize: 11, fontFamily: 'monospace', lineHeight: 1.4,
          color: COLOR_TEXT_MUTED,
        }}>
          At aircraft:{' '}
          <span style={{ color: READOUT_VALUE_COLOR }}>{fmt(qnhAtAircraftHpa)}</span>
        </div>
      )}

      {stationHpa !== null && (
        <div style={{
          fontSize: 11, fontFamily: 'monospace', lineHeight: 1.4,
          color: COLOR_TEXT_MUTED,
        }}>
          At {stationIcao} ({Math.round(stationElevM * M_TO_FT).toLocaleString()} ft):{' '}
          <span style={{ color: READOUT_VALUE_COLOR }}>{fmt(stationHpa)}</span>
        </div>
      )}
    </div>
  )
}
