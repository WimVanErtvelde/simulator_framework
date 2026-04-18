// Left column of WeatherPanelV2 Global tab. Populated in 5a-iii (cloud
// properties) and 5a-iv (wind properties). Currently a placeholder.
//
// When a cloud or wind layer is selected in the middle MSL-graph column,
// this column will show editable properties for that layer (type, coverage,
// bases/tops for clouds; direction/speed/turbulence for winds).

export default function LayerPropertiesColumn() {
  return (
    <div style={{
      padding: 12,
      background: '#0d1117',
      border: '1px solid #1e293b',
      borderRadius: 3,
      color: '#64748b',
      fontFamily: 'monospace',
      fontSize: 12,
      height: '100%',
      minHeight: 0,
      overflowY: 'auto',
    }}>
      <div style={{
        fontSize: 11, fontWeight: 700, letterSpacing: 2,
        color: '#39d0d8', textTransform: 'uppercase',
        padding: '4px 0 6px', borderBottom: '1px solid #1e293b', marginBottom: 10,
      }}>Layer Properties</div>
      <div style={{ color: '#94a3b8' }}>Add or select a layer to edit.</div>
      <div style={{ marginTop: 16, fontSize: 10, color: '#475569', fontStyle: 'italic' }}>
        Populated in Slice 5a-iii (clouds) / 5a-iv (winds).
      </div>
    </div>
  )
}
