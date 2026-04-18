import { useState } from 'react'
import { useWeatherV2Store } from '../../../../store/useWeatherV2Store'
import { formatPressure } from '../weatherUnits'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider } from './fieldStyles'
import { UnitToggle } from './fieldCommon'

const UNIT_OPTIONS = [{ id: 'hPa', label: 'hPa' }, { id: 'inHg', label: 'inHg' }]

export default function PressureField() {
  const pressure_hpa = useWeatherV2Store(s => s.draft.global.pressure_hpa)
  const updateDraft  = useWeatherV2Store(s => s.updateDraft)
  const [unit, setUnit] = useState('hPa')

  return (
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>QNH</span>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span style={fieldValue}>{formatPressure(pressure_hpa, unit)}</span>
          <UnitToggle options={UNIT_OPTIONS} value={unit} onChange={setUnit} />
        </div>
      </div>
      <input
        type="range"
        min={880} max={1075} step={0.1}
        value={pressure_hpa}
        onChange={(e) => updateDraft(['global', 'pressure_hpa'], Number(e.target.value))}
        style={slider}
      />
    </div>
  )
}
