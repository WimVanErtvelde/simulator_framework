export default function GlobalTab() {
  return (
    <div style={{ fontFamily: 'monospace' }}>
      <div style={{
        fontSize: 11, fontWeight: 700, letterSpacing: 2,
        color: '#39d0d8', textTransform: 'uppercase',
        padding: '4px 0 6px', borderBottom: '1px solid #1e293b', marginBottom: 10,
      }}>Global Weather</div>
      <p style={{ fontSize: 12, color: '#94a3b8', lineHeight: 1.5, margin: '0 0 8px' }}>
        Global weather authoring — atmospheric scalars (Slice 5a-ii),
        cloud layers (Slice 5a-iii), and wind layers (Slice 5a-iv).
      </p>
      <p style={{ fontSize: 10, color: '#475569', fontStyle: 'italic', margin: 0 }}>
        Placeholder shell — see WeatherPanelV2 task plan.
      </p>
    </div>
  )
}
