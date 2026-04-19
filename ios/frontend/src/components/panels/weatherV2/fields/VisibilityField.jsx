import { useState } from 'react'
import { VISIBILITY_PRESETS } from '../weatherPresets'
import { formatVisibility, isRvrMode } from '../weatherUnits'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider } from './fieldStyles'
import { PillGroup, PresetChip } from './fieldCommon'
import OverridePill from './OverridePill'

const UNIT_OPTIONS = [{ id: 'm', label: 'm' }, { id: 'SM', label: 'SM' }]

// Scalar field — accepts the authored value via props so it can be
// reused in patch tab context. Override/inherit pill dormant on Global.
export default function VisibilityField({
  value,
  onChange,
  showOverrideToggle = false,
  overrideEnabled   = true,
  onToggleOverride  = () => {},
}) {
  const [unit, setUnit] = useState('m')
  const disabled = !overrideEnabled

  const rvr = isRvrMode(value)

  return (
    <div style={{ ...fieldBox, opacity: disabled ? 0.4 : 1 }}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>{rvr ? 'RVR' : 'Visibility'}</span>
        {showOverrideToggle && (
          <OverridePill enabled={overrideEnabled} onToggle={onToggleOverride} />
        )}
        <span style={fieldValue}>{formatVisibility(value, unit)}</span>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 100px', gap: 8, alignItems: 'center' }}>
        <input
          type="range"
          min={0} max={160000}
          step={rvr ? 25 : 100}
          value={value}
          disabled={disabled}
          onChange={(e) => onChange(Number(e.target.value))}
          style={slider}
        />
        <PillGroup options={UNIT_OPTIONS} value={unit} onChange={setUnit} />
      </div>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4 }}>
        {VISIBILITY_PRESETS.map(p => (
          <PresetChip
            key={p.id}
            label={p.label}
            onClick={disabled ? undefined : () => onChange(p.visibility_m)}
          />
        ))}
      </div>
    </div>
  )
}
