import { useWeatherV2Store } from '../../../../store/useWeatherV2Store'
import { RUNWAY_FRICTIONS, RUNWAY_CONDITIONS } from '../weatherPresets'
import { fieldBox, fieldHeader, fieldLabel } from './fieldStyles'
import { EnumChips } from './fieldCommon'

export default function RunwayField() {
  const runway_friction   = useWeatherV2Store(s => s.draft.global.runway_friction)
  const runway_conditions = useWeatherV2Store(s => s.draft.global.runway_conditions)
  const updateDraft       = useWeatherV2Store(s => s.updateDraft)

  return (
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>Runway</span>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
          <span style={{
            fontSize: 10, color: '#64748b', fontFamily: 'monospace',
            letterSpacing: 1, textTransform: 'uppercase',
          }}>Friction</span>
          <EnumChips
            options={RUNWAY_FRICTIONS}
            value={runway_friction}
            onChange={(v) => updateDraft(['global', 'runway_friction'], v)}
          />
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
          <span style={{
            fontSize: 10, color: '#64748b', fontFamily: 'monospace',
            letterSpacing: 1, textTransform: 'uppercase',
          }}>Coverage</span>
          <EnumChips
            options={RUNWAY_CONDITIONS}
            value={runway_conditions}
            onChange={(v) => updateDraft(['global', 'runway_conditions'], v)}
          />
        </div>
      </div>
      <div style={{
        fontSize: 10, color: '#64748b', fontFamily: 'monospace', fontStyle: 'italic',
      }}>
        Coverage (Uniform/Patchy) is UI-only — no wire field yet.
      </div>
    </div>
  )
}
