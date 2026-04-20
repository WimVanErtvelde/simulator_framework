import { fieldBox, fieldHeader, fieldLabel, fieldValue, neutralBtn,
         COLOR_ACCENT_TEAL, COLOR_BG_CARD, COLOR_BORDER, COLOR_TEXT_MUTED } from './fieldStyles'
import OverridePill from './OverridePill'

// Runway condition — 6-category × 3-severity model, mapped to the 0-15
// runway_friction index on the wire. Same semantics as WX but decoupled
// from the store: takes value + onChange as props.
//
// Commits atomically on Accept alongside atmospheric scalars and cloud
// layers (Slice 5a-iii.1).

const RUNWAY_CATEGORIES = [
  { id: 'DRY',   label: 'DRY',   color: '#22c55e', baseIndex:  0, noSeverity: true },
  { id: 'WET',   label: 'WET',   color: '#39d0d8', baseIndex:  1 },
  { id: 'WATER', label: 'WATER', color: '#06b6d4', baseIndex:  4 },
  { id: 'SNOW',  label: 'SNOW',  color: '#f1f5f9', baseIndex:  7 },
  { id: 'ICE',   label: 'ICE',   color: '#93c5fd', baseIndex: 10 },
  { id: 'SN+IC', label: 'SN+IC', color: '#c084fc', baseIndex: 13 },
]
const SEVERITIES = ['LIGHT', 'MEDIUM', 'MAX']

function runwayIndexFromCategorySeverity(categoryId, severity) {
  const cat = RUNWAY_CATEGORIES.find(c => c.id === categoryId) ?? RUNWAY_CATEGORIES[0]
  if (cat.noSeverity) return cat.baseIndex
  const sevIdx = Math.max(0, SEVERITIES.indexOf(severity))
  return cat.baseIndex + sevIdx
}

function runwayDescribe(index) {
  const idx = Math.max(0, Math.min(15, Number(index) || 0))
  if (idx === 0) return { categoryId: 'DRY', severity: null, label: 'DRY' }
  const group  = Math.floor((idx - 1) / 3)
  const offset = (idx - 1) % 3
  const cat      = RUNWAY_CATEGORIES[1 + group]
  const severity = SEVERITIES[offset]
  return { categoryId: cat.id, severity, label: `${cat.label} (${severity.toLowerCase()})` }
}

export default function RunwayField({
  value,
  onChange,
  showOverrideToggle = false,
  overrideEnabled    = true,
  onToggleOverride   = () => {},
}) {
  const disabled     = !overrideEnabled
  const runwayIndex  = Number(value) || 0
  const runwayActive = runwayDescribe(runwayIndex)

  const selectRunwayCategory = (categoryId) => {
    if (disabled) return
    const cat = RUNWAY_CATEGORIES.find(c => c.id === categoryId)
    if (!cat) return
    // DRY is a single bucket; non-DRY defaults to MEDIUM unless the user
    // re-taps the currently active category — then keep the current severity
    // so repeated taps don't stomp a LIGHT/MAX selection.
    let severity = 'MEDIUM'
    if (!cat.noSeverity && runwayActive.categoryId === categoryId && runwayActive.severity) {
      severity = runwayActive.severity
    }
    onChange(runwayIndexFromCategorySeverity(categoryId, severity))
  }

  const selectRunwaySeverity = (severity) => {
    if (disabled) return
    if (runwayActive.categoryId === 'DRY') return
    onChange(runwayIndexFromCategorySeverity(runwayActive.categoryId, severity))
  }

  return (
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span style={fieldLabel}>Runway Condition</span>
          {showOverrideToggle && (
            <OverridePill enabled={overrideEnabled} onToggle={onToggleOverride} />
          )}
        </div>
        <span style={fieldValue}>{runwayActive.label}</span>
      </div>

      <div style={{ opacity: disabled ? 0.4 : 1,
                    display: 'flex', flexDirection: 'column', gap: 4 }}>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 4 }}>
        {RUNWAY_CATEGORIES.map(cat => {
          const active = runwayActive.categoryId === cat.id
          return (
            <button
              key={cat.id}
              type="button"
              disabled={disabled}
              onClick={() => selectRunwayCategory(cat.id)}
              onTouchEnd={(e) => { e.preventDefault(); selectRunwayCategory(cat.id) }}
              style={{
                ...neutralBtn, height: 36, fontSize: 11,
                background:  active ? `${cat.color}22` : COLOR_BG_CARD,
                borderColor: active ? cat.color : COLOR_BORDER,
                color:       active ? cat.color : COLOR_TEXT_MUTED,
                cursor: disabled ? 'not-allowed' : 'pointer',
              }}
            >{cat.label}</button>
          )
        })}
      </div>

      {/* Severity row stays mounted (disabled for DRY) so the card height
          doesn't jitter when the category changes. */}
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 4 }}>
        {SEVERITIES.map(sev => {
          const dry    = runwayActive.categoryId === 'DRY'
          const active = !dry && runwayActive.severity === sev
          const cat    = RUNWAY_CATEGORIES.find(c => c.id === runwayActive.categoryId)
          const color  = cat?.color ?? COLOR_ACCENT_TEAL
          const rowDisabled = disabled || dry
          return (
            <button
              key={sev}
              type="button"
              disabled={rowDisabled}
              onClick={() => selectRunwaySeverity(sev)}
              onTouchEnd={(e) => { e.preventDefault(); if (!rowDisabled) selectRunwaySeverity(sev) }}
              style={{
                ...neutralBtn, height: 32, fontSize: 10,
                background:  active ? `${color}22` : COLOR_BG_CARD,
                borderColor: active ? color : COLOR_BORDER,
                color:       active ? color : COLOR_TEXT_MUTED,
                opacity: dry ? 0.35 : 1,
                cursor: rowDisabled ? 'not-allowed' : 'pointer',
              }}
            >{sev}</button>
          )
        })}
      </div>
      </div>
    </div>
  )
}
