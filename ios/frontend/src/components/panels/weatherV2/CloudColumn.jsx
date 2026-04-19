import { useSimStore } from '../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import LayerBand from './graph/LayerBand'
import { MAX_FT, MIN_FT, M_TO_FT, FT_TO_M } from './graph/mslScale'
import { cloudTypeInfo } from './weatherPresets'

const MAX_CLOUDS = 3          // backend-enforced cap (ios_backend_node.py:1184)
const MIN_THICKNESS_FT = 100  // floor for drag-resize

// Middle-column cloud graph. Reads cloud_layers + stationElevationM from the
// shared useSimStore.activeWeather slice (same broadcast WX consumes) and
// authors edits via the existing add_cloud_layer / remove_cloud_layer WS
// handlers — no new backend plumbing. Drag updates are committed once at
// pointer-up (LayerBand handles the preview locally).
export default function CloudColumn({ height, width }) {
  const { cloudLayers, stationElevM, ws, wsConnected } = useSimStore(useShallow(s => ({
    cloudLayers:   s.activeWeather?.cloudLayers ?? [],
    stationElevM:  s.activeWeather?.stationElevationM ?? 0,
    ws:            s.ws,
    wsConnected:   s.wsConnected,
  })))
  const { selectedLayer, selectLayer } = useWeatherV2Store(useShallow(s => ({
    selectedLayer: s.selectedLayer,
    selectLayer:   s.selectLayer,
  })))

  const canAdd = cloudLayers.length < MAX_CLOUDS
  const stationElevFt = stationElevM * M_TO_FT

  const sendMsg = (type, data) => {
    if (!ws || !wsConnected) return
    ws.send(JSON.stringify({ type, data }))
  }

  const sendAdd = () => {
    if (!canAdd) return
    sendMsg('add_cloud_layer', {
      cloud_type: 7,                     // Cumulus default
      coverage_pct: 25,
      base_agl_ft: 3000,
      thickness_m: 2000 * FT_TO_M,
      transition_band_m: 200 * FT_TO_M,
      scud_enable: false,
      scud_frequency_pct: 0,
    })
  }

  // Update is remove-then-re-add — the legacy backend path has no update
  // handler. One remove + one add per drag commit, so X-Plane sees at most
  // one churn cycle per user interaction (Slice 5a-iii MVP limitation).
  const sendUpdate = (index, patch) => {
    const cur = cloudLayers[index]
    if (!cur) return
    sendMsg('remove_cloud_layer', { index })
    sendMsg('add_cloud_layer', {
      cloud_type:         patch.cloud_type        ?? cur.cloud_type,
      coverage_pct:       patch.coverage_pct      ?? cur.coverage_pct,
      base_agl_ft:        patch.base_agl_ft       ?? cur.base_agl_ft,
      thickness_m:        patch.thickness_m       ?? cur.thickness_m,
      transition_band_m:  200 * FT_TO_M,
      scud_enable:        false,
      scud_frequency_pct: 0,
    })
  }

  // Clamp a candidate base_agl_ft so the band stays within [MIN_FT, MAX_FT]
  // on both top and bottom. Returns a clamped agl-ft value.
  const clampBaseAgl = (baseAglFt, thicknessFt) => {
    const minBaseAgl = MIN_FT - stationElevFt
    const maxBaseAgl = MAX_FT - stationElevFt - thicknessFt
    return Math.max(minBaseAgl, Math.min(maxBaseAgl, baseAglFt))
  }

  return (
    <div style={{
      position: 'relative',
      width,
      height,
      borderLeft: '1px dashed #1e293b',
      background: '#050810',
      flexShrink: 0,
    }}>
      {/* + Cloud Layer button — floats just above the graph body */}
      <button
        type="button"
        disabled={!canAdd}
        onClick={sendAdd}
        onTouchEnd={(e) => { e.preventDefault(); if (canAdd) sendAdd() }}
        style={{
          position: 'absolute',
          top: -36, left: 0,
          height: 28, padding: '0 12px',
          background: canAdd ? '#111827' : '#0b1220',
          border: `1px solid ${canAdd ? '#1e293b' : '#111827'}`,
          borderRadius: 3,
          color: canAdd ? '#39d0d8' : '#334155',
          fontFamily: 'monospace', fontSize: 11, fontWeight: 700,
          letterSpacing: 1, textTransform: 'uppercase',
          cursor: canAdd ? 'pointer' : 'not-allowed',
          touchAction: 'manipulation',
        }}
      >+ Cloud Layer</button>

      {cloudLayers.map((cl, i) => {
        const thicknessFt = (cl.thickness_m ?? 0) * M_TO_FT
        const bottomFt    = (cl.base_agl_ft ?? 0) + stationElevFt
        const topFt       = bottomFt + thicknessFt
        const info        = cloudTypeInfo(cl.cloud_type ?? 7)
        const cov         = Math.round(cl.coverage_pct ?? 0)
        const label       = `${info.label} ${cov}%`
        const isSelected  = selectedLayer?.kind === 'cloud' && selectedLayer?.index === i

        return (
          <LayerBand
            key={i}
            kind="cloud"
            topFt={topFt}
            bottomFt={bottomFt}
            height={height}
            width={width}
            selected={isSelected}
            label={label}
            color={info.color}
            onSelect={() => selectLayer('cloud', i)}
            onCommitMove={(deltaFt) => {
              const newBaseAgl = clampBaseAgl(
                (cl.base_agl_ft ?? 0) + deltaFt,
                thicknessFt,
              )
              sendUpdate(i, { base_agl_ft: newBaseAgl })
            }}
            onCommitResizeTop={(newTopFt) => {
              const newThicknessFt = Math.max(MIN_THICKNESS_FT, newTopFt - bottomFt)
              sendUpdate(i, { thickness_m: newThicknessFt * FT_TO_M })
            }}
            onCommitResizeBottom={(newBotFt) => {
              const newThicknessFt = Math.max(MIN_THICKNESS_FT, topFt - newBotFt)
              const newBaseAgl = newBotFt - stationElevFt
              sendUpdate(i, {
                base_agl_ft: newBaseAgl,
                thickness_m: newThicknessFt * FT_TO_M,
              })
            }}
          />
        )
      })}
    </div>
  )
}
