import { useState } from 'react'
import { useSimStore } from '../../../../store/useSimStore'
import { useWeatherV2Store } from '../../../../store/useWeatherV2Store'
import { formatPressure, hpaToInHg } from '../weatherUnits'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider,
         COLOR_TEXT_MUTED } from './fieldStyles'
import { PillGroup } from './fieldCommon'

const UNIT_OPTIONS = [{ id: 'hPa', label: 'hPa' }, { id: 'inHg', label: 'inHg' }]
const READOUT_VALUE_COLOR = '#94a3b8'

// Authors sea-level pressure. "At aircraft" readout shows the live
// atmosphere broadcast QNH at the aircraft's position. Unit toggle
// formats both the authored value and the readout.
export default function PressureField() {
  const pressure_hpa = useWeatherV2Store(s => s.draft.global.pressure_hpa)
  const updateDraft  = useWeatherV2Store(s => s.updateDraft)
  const [unit, setUnit] = useState('hPa')

  const qnhAtAircraftHpa = useSimStore(s => s.atmosphere?.qnhHpa)

  const fmt = (hpa) => unit === 'inHg'
    ? `${hpaToInHg(hpa).toFixed(2)} inHg`
    : `${hpa.toFixed(1)} hPa`

  return (
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>Sea Level Press</span>
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

      {Number.isFinite(qnhAtAircraftHpa) && (
        <div style={{
          fontSize: 11, fontFamily: 'monospace', lineHeight: 1.4,
          color: COLOR_TEXT_MUTED,
        }}>
          At aircraft:{' '}
          <span style={{ color: READOUT_VALUE_COLOR }}>{fmt(qnhAtAircraftHpa)}</span>
        </div>
      )}
    </div>
  )
}
