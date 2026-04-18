import { useState } from 'react'
import { useWeatherV2Store } from '../../../../store/useWeatherV2Store'
import { formatPressure } from '../weatherUnits'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider } from './fieldStyles'
import { PillGroup } from './fieldCommon'

const UNIT_OPTIONS = [{ id: 'hPa', label: 'hPa' }, { id: 'inHg', label: 'inHg' }]

export default function PressureField() {
  const pressure_hpa = useWeatherV2Store(s => s.draft.global.pressure_hpa)
  const updateDraft  = useWeatherV2Store(s => s.updateDraft)
  const [unit, setUnit] = useState('hPa')

  return (
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>QNH</span>
        <span style={fieldValue}>{formatPressure(pressure_hpa, unit)}</span>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 110px', gap: 8, alignItems: 'center' }}>
        <input
          type="range"
          min={880} max={1075} step={0.1}
          value={pressure_hpa}
          onChange={(e) => updateDraft(['global', 'pressure_hpa'], Number(e.target.value))}
          style={slider}
        />
        <PillGroup options={UNIT_OPTIONS} value={unit} onChange={setUnit} />
      </div>
    </div>
  )
}
