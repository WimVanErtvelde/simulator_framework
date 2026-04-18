import { useState } from 'react'
import { useWeatherV2Store } from '../../../../store/useWeatherV2Store'
import { VISIBILITY_PRESETS } from '../weatherPresets'
import { formatVisibility, isRvrMode } from '../weatherUnits'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider } from './fieldStyles'
import { UnitToggle, PresetChip } from './fieldCommon'

const UNIT_OPTIONS = [{ id: 'm', label: 'm' }, { id: 'SM', label: 'SM' }]

export default function VisibilityField() {
  const visibility_m = useWeatherV2Store(s => s.draft.global.visibility_m)
  const updateDraft  = useWeatherV2Store(s => s.updateDraft)
  const [unit, setUnit] = useState('m')

  const rvr = isRvrMode(visibility_m)

  return (
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>{rvr ? 'RVR' : 'Visibility'}</span>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span style={fieldValue}>{formatVisibility(visibility_m, unit)}</span>
          <UnitToggle options={UNIT_OPTIONS} value={unit} onChange={setUnit} />
        </div>
      </div>
      <input
        type="range"
        min={0} max={160000}
        step={rvr ? 25 : 100}
        value={visibility_m}
        onChange={(e) => updateDraft(['global', 'visibility_m'], Number(e.target.value))}
        style={slider}
      />
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4 }}>
        {VISIBILITY_PRESETS.map(p => (
          <PresetChip
            key={p.id}
            label={p.label}
            onClick={() => updateDraft(['global', 'visibility_m'], p.visibility_m)}
          />
        ))}
      </div>
    </div>
  )
}
