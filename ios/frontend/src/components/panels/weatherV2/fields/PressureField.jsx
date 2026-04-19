import { useState } from 'react'
import { useSimStore } from '../../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { useWeatherV2Store } from '../../../../store/useWeatherV2Store'
import { formatPressure, hpaToInHg, HPA_TO_PA, isaPressureAt } from '../weatherUnits'
import { M_TO_FT } from '../graph/mslScale'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider,
         COLOR_TEXT_MUTED, COLOR_TEXT_DIM } from './fieldStyles'
import { PillGroup } from './fieldCommon'

const UNIT_OPTIONS = [{ id: 'hPa', label: 'hPa' }, { id: 'inHg', label: 'inHg' }]
const READOUT_VALUE_COLOR = '#94a3b8'

// Authors sea-level pressure. Derived readouts show the altimeter
// setting reduced to station elevation (ISA) and the live atmosphere
// broadcast value at the aircraft's position. Toggle respects both.
export default function PressureField() {
  const pressure_hpa = useWeatherV2Store(s => s.draft.global.pressure_hpa)
  const updateDraft  = useWeatherV2Store(s => s.updateDraft)
  const [unit, setUnit] = useState('hPa')

  const { stationIcao, stationElevM, qnhAtAircraftHpa } =
    useSimStore(useShallow(s => ({
      stationIcao:      s.activeWeather?.stationIcao ?? '',
      stationElevM:     s.activeWeather?.stationElevationM ?? 0,
      qnhAtAircraftHpa: s.atmosphere?.qnhHpa,
    })))

  const slPressurePa       = pressure_hpa * HPA_TO_PA
  const stationPressureHpa = isaPressureAt(slPressurePa, stationElevM) / HPA_TO_PA
  const stationElevFt      = Math.round(stationElevM * M_TO_FT)

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

      <div style={{
        fontSize: 11, fontFamily: 'monospace', lineHeight: 1.4,
        color: COLOR_TEXT_MUTED,
      }}>
        {stationIcao ? (
          <div>
            Altimeter at {stationIcao} ({stationElevFt.toLocaleString()} ft):{' '}
            <span style={{ color: READOUT_VALUE_COLOR }}>
              {fmt(stationPressureHpa)}
            </span>
          </div>
        ) : (
          <div style={{ color: COLOR_TEXT_DIM }}>
            Altimeter at station: (no station set)
          </div>
        )}
        {Number.isFinite(qnhAtAircraftHpa) && (
          <div>
            At aircraft:{' '}
            <span style={{ color: READOUT_VALUE_COLOR }}>
              {fmt(qnhAtAircraftHpa)}
            </span>
          </div>
        )}
      </div>
    </div>
  )
}
