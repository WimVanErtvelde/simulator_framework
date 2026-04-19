import { PRECIP_TYPES } from '../weatherPresets'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider } from './fieldStyles'
import { PillGroup } from './fieldCommon'
import OverridePill from './OverridePill'

// Accepts rate as 0.0–1.0 and fires onChangeRate with 0.0–1.0. Slider
// UI renders 0–100 % for readability.
export default function PrecipitationField({
  rate,
  type,
  onChangeRate,
  onChangeType,
  showOverrideToggle = false,
  overrideEnabled    = true,
  onToggleOverride   = () => {},
}) {
  const disabled = !overrideEnabled
  const pct = Math.round(rate * 100)

  return (
    <div style={{ ...fieldBox, opacity: disabled ? 0.4 : 1 }}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>Precipitation</span>
        {showOverrideToggle && (
          <OverridePill enabled={overrideEnabled} onToggle={onToggleOverride} />
        )}
        <span style={fieldValue}>{pct} %</span>
      </div>
      <PillGroup
        options={PRECIP_TYPES}
        value={type}
        onChange={disabled ? () => {} : onChangeType}
      />
      <input
        type="range"
        min={0} max={100} step={1}
        value={pct}
        disabled={disabled}
        onChange={(e) => onChangeRate(Number(e.target.value) / 100)}
        style={slider}
      />
    </div>
  )
}
