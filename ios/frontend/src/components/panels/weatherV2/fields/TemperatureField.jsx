import { useWeatherV2Store } from '../../../../store/useWeatherV2Store'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider } from './fieldStyles'

export default function TemperatureField() {
  const temperature_c = useWeatherV2Store(s => s.draft.global.temperature_c)
  const updateDraft   = useWeatherV2Store(s => s.updateDraft)

  return (
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>Temperature</span>
        <span style={fieldValue}>{temperature_c.toFixed(0)} °C</span>
      </div>
      <input
        type="range"
        min={-100} max={60} step={1}
        value={temperature_c}
        onChange={(e) => updateDraft(['global', 'temperature_c'], Number(e.target.value))}
        style={slider}
      />
    </div>
  )
}
