import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider } from './fieldStyles'
import OverridePill from './OverridePill'

export default function HumidityField({
  value,
  onChange,
  showOverrideToggle = false,
  overrideEnabled    = true,
  onToggleOverride   = () => {},
}) {
  const disabled = !overrideEnabled

  return (
    <div style={{ ...fieldBox, opacity: disabled ? 0.4 : 1 }}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>Humidity</span>
        {showOverrideToggle && (
          <OverridePill enabled={overrideEnabled} onToggle={onToggleOverride} />
        )}
        <span style={fieldValue}>{Math.round(value)} %</span>
      </div>
      <input
        type="range"
        min={0} max={100} step={1}
        value={value}
        disabled={disabled}
        onChange={(e) => onChange(Number(e.target.value))}
        style={slider}
      />
    </div>
  )
}
