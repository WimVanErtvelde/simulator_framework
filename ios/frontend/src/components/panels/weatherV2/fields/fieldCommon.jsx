// Small shared primitives for Slice 5a-ii+ field components.
// Styles live in fieldStyles.js so this file can export only components
// (react-refresh requirement).
//
// PillGroup is a byte-for-byte port of WeatherPanel.jsx's PillGroup (which
// is private there). Kept duplicated so V1/V2 stay decoupled during
// development; consolidation into PanelUtils will happen when V2 replaces V1.

import { neutralBtn, COLOR_ACCENT_TEAL, COLOR_BG_CARD, COLOR_BORDER, COLOR_TEXT_MUTED } from './fieldStyles'

export function PillGroup({ options, value, onChange, color = COLOR_ACCENT_TEAL }) {
  return (
    <div style={{ display: 'grid', gridTemplateColumns: `repeat(${options.length}, 1fr)`, gap: 4 }}>
      {options.map(opt => {
        const active = value === opt.id
        return (
          <button
            key={opt.id}
            type="button"
            onClick={() => onChange(opt.id)}
            onTouchEnd={(e) => { e.preventDefault(); onChange(opt.id) }}
            style={{
              ...neutralBtn, height: 32, fontSize: 11,
              background: active ? 'rgba(57, 208, 216, 0.10)' : COLOR_BG_CARD,
              borderColor: active ? color : COLOR_BORDER,
              color:       active ? color : COLOR_TEXT_MUTED,
            }}
          >{opt.label}</button>
        )
      })}
    </div>
  )
}

// One-shot chip for visibility presets (VFR / CAT III / etc.). Not a toggle —
// pressing applies the value and returns to neutral. Hover highlight for
// affordance.
export function PresetChip({ label, onClick }) {
  return (
    <button
      type="button"
      onClick={onClick}
      onTouchEnd={(e) => { e.preventDefault(); onClick?.() }}
      style={{
        ...neutralBtn,
        height: 24, padding: '0 8px', fontSize: 10,
        background: COLOR_BG_CARD,
      }}
      onMouseEnter={(e) => {
        e.currentTarget.style.background = 'rgba(57, 208, 216, 0.10)'
        e.currentTarget.style.color = COLOR_ACCENT_TEAL
        e.currentTarget.style.borderColor = COLOR_ACCENT_TEAL
      }}
      onMouseLeave={(e) => {
        e.currentTarget.style.background = COLOR_BG_CARD
        e.currentTarget.style.color = COLOR_TEXT_MUTED
        e.currentTarget.style.borderColor = COLOR_BORDER
      }}
    >{label}</button>
  )
}
