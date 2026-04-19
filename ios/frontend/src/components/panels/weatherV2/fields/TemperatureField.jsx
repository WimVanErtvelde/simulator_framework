import { useSimStore } from '../../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { useWeatherV2Store } from '../../../../store/useWeatherV2Store'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider,
         COLOR_TEXT_MUTED } from './fieldStyles'

const READOUT_VALUE_COLOR = '#94a3b8'

// Authors sea-level temperature. "At aircraft" readout resolves the
// authored SL value to the aircraft's current position via the live
// atmosphere broadcast.
export default function TemperatureField() {
  const temperature_c = useWeatherV2Store(s => s.draft.global.temperature_c)
  const updateDraft   = useWeatherV2Store(s => s.updateDraft)

  const { oatAtAircraftC, altFtAgl } = useSimStore(useShallow(s => ({
    oatAtAircraftC: s.atmosphere?.oatCelsius,   // may be undefined until broadcast
    altFtAgl:       s.fdm?.altFtAgl ?? 0,
  })))

  const aircraftAglFt = Math.round(altFtAgl)

  return (
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>Sea Level Temp</span>
        <span style={fieldValue}>{temperature_c.toFixed(0)} °C</span>
      </div>
      <input
        type="range"
        min={-100} max={60} step={1}
        value={temperature_c}
        onChange={(e) => updateDraft(['global', 'temperature_c'], Number(e.target.value))}
        style={slider}
      />

      {Number.isFinite(oatAtAircraftC) && (
        <div style={{
          fontSize: 11, fontFamily: 'monospace', lineHeight: 1.4,
          color: COLOR_TEXT_MUTED,
        }}>
          At aircraft ({aircraftAglFt.toLocaleString()} ft AGL):{' '}
          <span style={{ color: READOUT_VALUE_COLOR }}>
            {oatAtAircraftC.toFixed(1)} °C
          </span>
        </div>
      )}
    </div>
  )
}
