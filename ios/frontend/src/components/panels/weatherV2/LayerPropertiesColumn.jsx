import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import CloudPropertiesEditor from './CloudPropertiesEditor'
import WindPropertiesEditor from './WindPropertiesEditor'

// Left column of WeatherPanelV2 Global tab. Dispatches on the current
// selection: empty-state message, cloud editor, or wind editor.
export default function LayerPropertiesColumn({ patchContext }) {
  const selectedLayer = useWeatherV2Store(s => s.selectedLayer)
  const activeTab     = useWeatherV2Store(s => s.activeTab)

  // Only honor a selection if it belongs to the active tab — otherwise
  // the index refers to a different patch's layer list. setActiveTab
  // already clears selection on switch, but a stale selection during a
  // race is safer handled here too.
  const showSel = selectedLayer?.tabId === activeTab

  let content
  if (!showSel) {
    content = (
      <div style={{
        color: '#64748b', fontFamily: 'monospace', fontSize: 12,
        padding: '20px 0',
      }}>Add or select a layer to edit.</div>
    )
  } else if (selectedLayer.kind === 'cloud') {
    content = <CloudPropertiesEditor index={selectedLayer.index} patchContext={patchContext} />
  } else if (selectedLayer.kind === 'wind') {
    content = <WindPropertiesEditor index={selectedLayer.index} patchContext={patchContext} />
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
