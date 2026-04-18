import { useWeatherV2Store } from '../../../../store/useWeatherV2Store'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider } from './fieldStyles'

export default function HumidityField() {
  const humidity_pct = useWeatherV2Store(s => s.draft.global.humidity_pct)
  const updateDraft  = useWeatherV2Store(s => s.updateDraft)

  return (
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>Humidity</span>
        <span style={fieldValue}>{Math.round(humidity_pct)} %</span>
      </div>
      <input
        type="range"
        min={0} max={100} step={1}
        value={humidity_pct}
        onChange={(e) => updateDraft(['global', 'humidity_pct'], Number(e.target.value))}
        style={slider}
      />
    </div>
  )
}
