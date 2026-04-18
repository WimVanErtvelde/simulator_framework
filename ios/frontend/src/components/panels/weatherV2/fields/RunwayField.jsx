import { useSimStore } from '../../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, neutralBtn,
         COLOR_ACCENT_TEAL, COLOR_BG_CARD, COLOR_BORDER, COLOR_TEXT_MUTED } from './fieldStyles'

// Runway condition — 6-category × 3-severity model, mapped to the 0-15
// runway_friction index on the wire. Ported directly from WeatherPanel.jsx
// so V1 (WX) and V2 (WX2) share the same semantics and same backend handler.
// State lives in useSimStore.activeWeather.runwayFriction — no draft/Accept;
// each tap fires set_runway_condition immediately.

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

export default function RunwayField() {
  const { ws, wsConnected, runwayFriction } = useSimStore(useShallow(s => ({
    ws: s.ws, wsConnected: s.wsConnected,
    runwayFriction: s.activeWeather?.runwayFriction ?? 0,
  })))

  const runwayIndex  = Number(runwayFriction) || 0
  const runwayActive = runwayDescribe(runwayIndex)

  const sendRunwayIndex = (index) => {
    if (!ws || !wsConnected) return
    ws.send(JSON.stringify({ type: 'set_runway_condition', data: { index } }))
  }

  const selectRunwayCategory = (categoryId) => {
    const cat = RUNWAY_CATEGORIES.find(c => c.id === categoryId)
    if (!cat) return
    // DRY is a single bucket; non-DRY defaults to MEDIUM unless the user
    // re-taps the currently active category — then keep the current severity
    // so repeated taps don't stomp a LIGHT/MAX selection.
    let severity = 'MEDIUM'
    if (!cat.noSeverity && runwayActive.categoryId === categoryId && runwayActive.severity) {
      severity = runwayActive.severity
    }
    sendRunwayIndex(runwayIndexFromCategorySeverity(categoryId, severity))
  }

  const selectRunwaySeverity = (severity) => {
    if (runwayActive.categoryId === 'DRY') return
    sendRunwayIndex(runwayIndexFromCategorySeverity(runwayActive.categoryId, severity))
  }

  return (
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>Runway Condition</span>
        <span style={fieldValue}>
          {runwayActive.label} <span style={{ color: '#475569' }}>— idx {runwayIndex}</span>
        </span>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 4 }}>
        {RUNWAY_CATEGORIES.map(cat => {
          const active = runwayActive.categoryId === cat.id
          return (
            <button
              key={cat.id}
              type="button"
              onClick={() => selectRunwayCategory(cat.id)}
              onTouchEnd={(e) => { e.preventDefault(); selectRunwayCategory(cat.id) }}
              style={{
                ...neutralBtn, height: 36, fontSize: 11,
                background:  active ? `${cat.color}22` : COLOR_BG_CARD,
                borderColor: active ? cat.color : COLOR_BORDER,
                color:       active ? cat.color : COLOR_TEXT_MUTED,
              }}
            >{cat.label}</button>
          )
        })}
      </div>

      {runwayActive.categoryId !== 'DRY' && (
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 4 }}>
          {SEVERITIES.map(sev => {
            const active = runwayActive.severity === sev
            const cat    = RUNWAY_CATEGORIES.find(c => c.id === runwayActive.categoryId)
            const color  = cat?.color ?? COLOR_ACCENT_TEAL
            return (
              <button
                key={sev}
                type="button"
                onClick={() => selectRunwaySeverity(sev)}
                onTouchEnd={(e) => { e.preventDefault(); selectRunwaySeverity(sev) }}
                style={{
                  ...neutralBtn, height: 32, fontSize: 10,
                  background:  active ? `${color}22` : COLOR_BG_CARD,
                  borderColor: active ? color : COLOR_BORDER,
                  color:       active ? color : COLOR_TEXT_MUTED,
                }}
              >{sev}</button>
            )
          })}
        </div>
      )}
    </div>
  )
}
