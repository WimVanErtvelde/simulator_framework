import { useSimStore } from '../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import { CLOUD_TYPES } from './weatherPresets'
import { M_TO_FT, FT_TO_M } from './graph/mslScale'

// Left-column editor for the selected cloud layer. Type picker, coverage
// slider, base/top MSL readouts, delete button. Base/top editing is via
// the graph drag; numeric input lives in a future slice.
export default function CloudPropertiesEditor({ index }) {
  const { cloudLayers, stationElevM, ws, wsConnected } = useSimStore(useShallow(s => ({
    cloudLayers:  s.activeWeather?.cloudLayers ?? [],
    stationElevM: s.activeWeather?.stationElevationM ?? 0,
    ws:           s.ws,
    wsConnected:  s.wsConnected,
  })))
  const clearSelection = useWeatherV2Store(s => s.clearSelection)

  const cl = cloudLayers[index]
  if (!cl) return null

  const type      = cl.cloud_type ?? 7
  const coverage  = Math.round(cl.coverage_pct ?? 0)
  const baseAglFt = cl.base_agl_ft ?? 0
  const thickFt   = (cl.thickness_m ?? 0) * M_TO_FT
  const stationFt = stationElevM * M_TO_FT
  const baseMslFt = Math.round(baseAglFt + stationFt)
  const topMslFt  = Math.round(baseMslFt + thickFt)

  const sendMsg = (t, data) => {
    if (!ws || !wsConnected) return
    ws.send(JSON.stringify({ type: t, data }))
  }

  const sendUpdate = (patch) => {
    sendMsg('remove_cloud_layer', { index })
    sendMsg('add_cloud_layer', {
      cloud_type:         patch.cloud_type   ?? type,
      coverage_pct:       patch.coverage_pct ?? coverage,
      base_agl_ft:        patch.base_agl_ft  ?? baseAglFt,
      thickness_m:        patch.thickness_m  ?? cl.thickness_m ?? 0,
      transition_band_m:  200 * FT_TO_M,
      scud_enable:        false,
      scud_frequency_pct: 0,
    })
  }

  const sendDelete = () => {
    sendMsg('remove_cloud_layer', { index })
    clearSelection()
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
      <div style={{
        fontSize: 11, fontWeight: 700, letterSpacing: 1,
        color: '#e2e8f0', textTransform: 'uppercase',
        fontFamily: 'monospace',
      }}>Cloud Layer {index + 1}</div>

      {/* Type picker — 2 × 3 grid */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <div style={{ fontSize: 10, color: '#64748b', fontFamily: 'monospace',
                      letterSpacing: 1, textTransform: 'uppercase' }}>Type</div>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 4 }}>
          {CLOUD_TYPES.map(t => {
            const active = t.id === type
            return (
              <button
                key={t.id}
                type="button"
                onClick={() => sendUpdate({ cloud_type: t.id })}
                onTouchEnd={(e) => { e.preventDefault(); sendUpdate({ cloud_type: t.id }) }}
                style={{
                  height: 28,
                  background: active ? `${t.color}22` : '#111827',
                  border: `1px solid ${active ? t.color : '#1e293b'}`,
                  borderRadius: 3,
                  color: active ? t.color : '#64748b',
                  fontSize: 11, fontFamily: 'monospace', fontWeight: 700,
                  letterSpacing: 0.5, textTransform: 'uppercase',
                  cursor: 'pointer', touchAction: 'manipulation',
                }}
              >{t.label}</button>
            )
          })}
        </div>
      </div>

      {/* Coverage slider */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between',
                      fontFamily: 'monospace' }}>
          <span style={{ fontSize: 10, color: '#64748b',
                         letterSpacing: 1, textTransform: 'uppercase' }}>Coverage</span>
          <span style={{ fontSize: 12, color: '#e2e8f0',
                         fontVariantNumeric: 'tabular-nums' }}>{coverage}%</span>
        </div>
        <input
          type="range"
          min={0} max={100} step={5}
          value={coverage}
          onChange={(e) => sendUpdate({ coverage_pct: Number(e.target.value) })}
          style={{
            width: '100%', accentColor: '#39d0d8',
            cursor: 'pointer', touchAction: 'manipulation',
          }}
        />
      </div>

      {/* Base / Top MSL readouts — drag in graph to edit */}
      <div style={{
        display: 'flex', flexDirection: 'column', gap: 2,
        padding: '6px 8px', background: '#111827', border: '1px solid #1e293b',
        borderRadius: 3,
        fontSize: 11, fontFamily: 'monospace',
        fontVariantNumeric: 'tabular-nums', color: '#94a3b8',
      }}>
        <div style={{ display: 'flex', justifyContent: 'space-between' }}>
          <span style={{ color: '#64748b' }}>Top</span>
          <span>{topMslFt.toLocaleString()} ft MSL</span>
        </div>
        <div style={{ display: 'flex', justifyContent: 'space-between' }}>
          <span style={{ color: '#64748b' }}>Base</span>
          <span>{baseMslFt.toLocaleString()} ft MSL</span>
        </div>
        <div style={{ fontSize: 10, color: '#475569', fontStyle: 'italic', marginTop: 2 }}>
          Drag band in graph to reposition
        </div>
      </div>

      <button
        type="button"
        onClick={sendDelete}
        onTouchEnd={(e) => { e.preventDefault(); sendDelete() }}
        style={{
          marginTop: 4, height: 32,
          background: 'rgba(239, 68, 68, 0.06)',
          border: '1px solid #7f1d1d',
          borderRadius: 3,
          color: '#f87171',
          fontFamily: 'monospace', fontSize: 11, fontWeight: 700,
          letterSpacing: 1, textTransform: 'uppercase',
          cursor: 'pointer', touchAction: 'manipulation',
        }}
      >Delete Layer</button>
    </div>
  )
}
