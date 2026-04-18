// Shared inline style tokens for WeatherPanelV2 field components.
// Byte-for-byte aligned with the WX panel's style vocabulary
// (see WeatherPanel.jsx neutralBtn + SectionHeader).
//
// Kept in a .js (no JSX) so react-refresh fast-refresh isn't broken by
// mixing constant exports with component exports in the same module.

// ── Color tokens ──────────────────────────────────────────────────────────
export const COLOR_BG_PANEL    = '#0d1117'
export const COLOR_BG_CARD     = '#111827'
export const COLOR_BG_INPUT    = '#1c2333'
export const COLOR_BORDER      = '#1e293b'
export const COLOR_TEXT_MUTED  = '#64748b'
export const COLOR_TEXT_DIM    = '#475569'
export const COLOR_TEXT_BRIGHT = '#e2e8f0'
export const COLOR_ACCENT_TEAL = '#39d0d8'
export const COLOR_ACCENT_PINK = '#bc4fcb'

// ── Card wrapper (one per field) ──────────────────────────────────────────
export const fieldBox = {
  padding: 10,
  background: COLOR_BG_PANEL,
  border: `1px solid ${COLOR_BORDER}`,
  borderRadius: 3,
  display: 'flex', flexDirection: 'column', gap: 6,
}

export const fieldHeader = {
  display: 'flex', alignItems: 'center', justifyContent: 'space-between',
  gap: 8,
}

export const fieldLabel = {
  fontSize: 11, fontWeight: 700, letterSpacing: 2,
  color: COLOR_ACCENT_TEAL, textTransform: 'uppercase', fontFamily: 'monospace',
}

export const fieldValue = {
  fontSize: 12, fontWeight: 700, color: COLOR_TEXT_BRIGHT,
  fontFamily: 'monospace', fontVariantNumeric: 'tabular-nums',
}

export const slider = {
  width: '100%',
  accentColor: COLOR_ACCENT_TEAL,
  cursor: 'pointer',
  touchAction: 'manipulation',
}

// WX's neutralBtn — the base for every chip-style button in V2 (PillGroup
// options, runway categories/severities, presets).
export const neutralBtn = {
  height: 36, background: COLOR_BG_INPUT, border: `1px solid ${COLOR_BORDER}`,
  borderRadius: 3,
  color: COLOR_TEXT_MUTED, fontSize: 12, fontFamily: 'monospace', fontWeight: 700,
  letterSpacing: 1, textTransform: 'uppercase',
  cursor: 'pointer', touchAction: 'manipulation',
}
