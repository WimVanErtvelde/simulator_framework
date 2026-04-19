import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import CloudPropertiesEditor from './CloudPropertiesEditor'

// Left column of WeatherPanelV2 Global tab. Dispatches on the current
// selection: empty-state message, cloud editor, or wind editor (stub).
export default function LayerPropertiesColumn() {
  const selectedLayer = useWeatherV2Store(s => s.selectedLayer)

  let content
  if (!selectedLayer) {
    content = (
      <div style={{
        color: '#64748b', fontFamily: 'monospace', fontSize: 12,
        padding: '20px 0',
      }}>Add or select a layer to edit.</div>
    )
  } else if (selectedLayer.kind === 'cloud') {
    content = <CloudPropertiesEditor index={selectedLayer.index} />
  } else if (selectedLayer.kind === 'wind') {
    content = (
      <div style={{ color: '#64748b', fontFamily: 'monospace', fontSize: 12 }}>
        Wind layer properties — Slice 5a-iv.
      </div>
    )
  }

  return (
    <div style={{
      padding: 12,
      background: '#0d1117',
      border: '1px solid #1e293b',
      borderRadius: 3,
      color: '#64748b',
      fontFamily: 'monospace',
      fontSize: 12,
      height: '100%', minHeight: 0,
      overflowY: 'auto',
    }}>
      <div style={{
        fontSize: 11, fontWeight: 700, letterSpacing: 2,
        color: '#39d0d8', textTransform: 'uppercase',
        padding: '4px 0 6px', borderBottom: '1px solid #1e293b', marginBottom: 10,
      }}>Layer Properties</div>
      {content}
    </div>
  )
}
