export default function PatchesTab() {
  return (
    <div style={{ fontFamily: 'monospace' }}>
      <div style={{
        fontSize: 11, fontWeight: 700, letterSpacing: 2,
        color: '#39d0d8', textTransform: 'uppercase',
        padding: '4px 0 6px', borderBottom: '1px solid #1e293b', marginBottom: 10,
      }}>Weather Patches</div>
      <p style={{ fontSize: 12, color: '#94a3b8', lineHeight: 1.5, margin: '0 0 8px' }}>
        Localized weather overrides at departure, destination, or custom locations.
      </p>
      <p style={{ fontSize: 10, color: '#475569', fontStyle: 'italic', margin: 0 }}>
        Comes in Slice 5b (depends on Slice 4 ios_backend patch lifecycle — done).
      </p>
    </div>
  )
}
