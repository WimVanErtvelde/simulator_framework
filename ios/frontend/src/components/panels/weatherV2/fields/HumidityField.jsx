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
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span style={fieldLabel}>Humidity</span>
          {showOverrideToggle && (
            <OverridePill enabled={overrideEnabled} onToggle={onToggleOverride} />
          )}
        </div>
        <span style={fieldValue}>{Math.round(value)} %</span>
      </div>
      <div style={{ opacity: disabled ? 0.4 : 1 }}>
        <input
          type="range"
          min={0} max={100} step={1}
          value={value}
          disabled={disabled}
          onChange={(e) => onChange(Number(e.target.value))}
          style={slider}
        />
      </div>
    </div>
  )
}
