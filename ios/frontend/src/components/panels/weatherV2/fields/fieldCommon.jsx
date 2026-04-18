// Small shared primitives used by every Slice 5a-ii field component.
// Styles live in fieldStyles.js so this file can export only components
// (react-refresh requirement).

// Small unit-toggle button (e.g. m/SM, hPa/inHg) — minimal footprint,
// lives to the right of the value display.
export function UnitToggle({ options, value, onChange }) {
  return (
    <div style={{ display: 'flex', gap: 0 }}>
      {options.map((opt, i) => {
        const active = value === opt.id
        const isFirst = i === 0
        const isLast  = i === options.length - 1
        return (
          <button
            key={opt.id}
            type="button"
            onClick={() => onChange(opt.id)}
            onTouchEnd={(e) => { e.preventDefault(); onChange(opt.id) }}
            style={{
              padding: '3px 8px', fontSize: 10, fontFamily: 'monospace',
              fontWeight: 700, letterSpacing: 1,
              border: '1px solid',
              borderTopLeftRadius:     isFirst ? 3 : 0,
              borderBottomLeftRadius:  isFirst ? 3 : 0,
              borderTopRightRadius:    isLast  ? 3 : 0,
              borderBottomRightRadius: isLast  ? 3 : 0,
              marginLeft: isFirst ? 0 : -1,
              cursor: 'pointer', touchAction: 'manipulation',
              borderColor: active ? '#39d0d8' : '#1e293b',
              background:  active ? 'rgba(57, 208, 216, 0.10)' : '#111827',
              color:       active ? '#39d0d8' : '#64748b',
              zIndex: active ? 1 : 0, position: 'relative',
            }}
          >{opt.label}</button>
        )
      })}
    </div>
  )
}

// Chip-style enum button row (Poor/Fair/Good, None/Rain/Snow/Sleet, etc.)
export function EnumChips({ options, value, onChange, color = '#39d0d8' }) {
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
              padding: '6px 4px', fontSize: 11, fontFamily: 'monospace',
              fontWeight: 700, letterSpacing: 1, textTransform: 'uppercase',
              border: '1px solid', borderRadius: 3,
              cursor: 'pointer', touchAction: 'manipulation',
              borderColor: active ? color : '#1e293b',
              background:  active ? `${color}1a` : '#111827',
              color:       active ? color : '#64748b',
            }}
          >{opt.label}</button>
        )
      })}
    </div>
  )
}

// Small preset chip (VFR / CAT III / etc.) — compact, not a toggle.
export function PresetChip({ label, onClick }) {
  return (
    <button
      type="button"
      onClick={onClick}
      onTouchEnd={(e) => { e.preventDefault(); onClick?.() }}
      style={{
        padding: '4px 8px', fontSize: 10, fontFamily: 'monospace',
        fontWeight: 700, letterSpacing: 1, textTransform: 'uppercase',
        border: '1px solid #1e293b', borderRadius: 3,
        background: '#111827', color: '#94a3b8',
        cursor: 'pointer', touchAction: 'manipulation',
      }}
      onMouseEnter={(e) => { e.currentTarget.style.background = 'rgba(57, 208, 216, 0.10)'; e.currentTarget.style.color = '#39d0d8'; e.currentTarget.style.borderColor = '#39d0d8' }}
      onMouseLeave={(e) => { e.currentTarget.style.background = '#111827'; e.currentTarget.style.color = '#94a3b8'; e.currentTarget.style.borderColor = '#1e293b' }}
    >{label}</button>
  )
}
