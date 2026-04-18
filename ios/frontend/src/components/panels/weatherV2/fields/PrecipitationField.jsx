import { useWeatherV2Store } from '../../../../store/useWeatherV2Store'
import { PRECIP_TYPES } from '../weatherPresets'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider } from './fieldStyles'
import { PillGroup } from './fieldCommon'

export default function PrecipitationField() {
  const precipitation_rate = useWeatherV2Store(s => s.draft.global.precipitation_rate)
  const precipitation_type = useWeatherV2Store(s => s.draft.global.precipitation_type)
  const updateDraft        = useWeatherV2Store(s => s.updateDraft)

  // Draft stores rate as 0.0-1.0; slider shows 0-100 %.
  const pct = Math.round(precipitation_rate * 100)

  return (
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>Precipitation</span>
        <span style={fieldValue}>{pct} %</span>
      </div>
      <input
        type="range"
        min={0} max={100} step={1}
        value={pct}
        onChange={(e) => updateDraft(['global', 'precipitation_rate'], Number(e.target.value) / 100)}
        style={slider}
      />
      <PillGroup
        options={PRECIP_TYPES}
        value={precipitation_type}
        onChange={(v) => updateDraft(['global', 'precipitation_type'], v)}
      />
    </div>
  )
}
