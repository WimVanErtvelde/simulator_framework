// Shared inline style tokens for WeatherPanelV2 field components.
// Slice 5a-ii.2 typography: labels recede (muted gray) so the larger,
// brighter value becomes the focal point of each card.
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

// ── Typography tokens ─────────────────────────────────────────────────────
export const FONT_LABEL = 11   // card header label (muted, uppercase)
export const FONT_VALUE = 17   // card header value (bright, tabular-nums)
export const FONT_BODY  = 11   // body copy / presets / pill button labels
export const FONT_META  = 11   // small meta text

// ── Geometry tokens ───────────────────────────────────────────────────────
export const CARD_PAD_X = 10
export const CARD_PAD_Y = 8
export const CARD_GAP   = 6

// ── Card wrapper (one per field) ──────────────────────────────────────────
export const fieldBox = {
  padding: `${CARD_PAD_Y}px ${CARD_PAD_X}px`,
  background: COLOR_BG_PANEL,
  border: `1px solid ${COLOR_BORDER}`,
  borderRadius: 3,
  display: 'flex', flexDirection: 'column', gap: CARD_GAP,
}

export const fieldHeader = {
  display: 'flex', alignItems: 'center', justifyContent: 'space-between',
  gap: 8,
}

export const fieldLabel = {
  fontSize: FONT_LABEL, fontWeight: 700, letterSpacing: 1,
  color: COLOR_TEXT_MUTED, textTransform: 'uppercase', fontFamily: 'monospace',
}

export const fieldValue = {
  fontSize: FONT_VALUE, fontWeight: 500, color: COLOR_TEXT_BRIGHT,
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
