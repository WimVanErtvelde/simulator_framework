// Shared inline style objects for WeatherPanelV2 field components.
// Kept in a .js (no JSX) so react-refresh fast-refresh isn't broken by
// mixing constant exports with component exports in the same module.

export const fieldBox = {
  background: '#0d1117',
  border: '1px solid #1e293b',
  borderRadius: 4,
  padding: 12,
  display: 'flex', flexDirection: 'column', gap: 8,
}

export const fieldHeader = {
  display: 'flex', alignItems: 'center', justifyContent: 'space-between',
  gap: 8,
}

export const fieldLabel = {
  fontSize: 10, fontWeight: 700, letterSpacing: 2,
  color: '#39d0d8', textTransform: 'uppercase', fontFamily: 'monospace',
}

export const fieldValue = {
  fontSize: 13, fontWeight: 700, color: '#e2e8f0',
  fontFamily: 'monospace', fontVariantNumeric: 'tabular-nums',
}

export const slider = {
  width: '100%',
  accentColor: '#39d0d8',
  cursor: 'pointer',
  touchAction: 'manipulation',
}
