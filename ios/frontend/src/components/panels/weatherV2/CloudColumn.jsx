import { useSimStore } from '../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import LayerBand from './graph/LayerBand'
import { MAX_FT, MIN_FT, M_TO_FT, FT_TO_M } from './graph/mslScale'
import { cloudTypeInfo } from './weatherPresets'

const MAX_CLOUDS       = 3
const MIN_THICKNESS_FT = 100

// Middle-column cloud graph. Reads cloud_layers from the V2 draft and
// dispatches updates via store actions — nothing touches the backend
// until Accept fires. Station elevation drives AGL↔MSL conversion:
//   - Global tab:  weather station elevation from activeWeather
//   - Patch tab:   patch.ground_elevation_m via patchContext prop
export default function CloudColumn({ height, width, patchContext }) {
  const globalStationElevM = useSimStore(s => s.activeWeather?.stationElevationM ?? 0)
  const activeTab          = useWeatherV2Store(s => s.activeTab)
  const patchCid           = patchContext?.client_id ?? null
  const stationElevM       = patchContext ? patchContext.stationElevM : globalStationElevM

  const { cloudLayers, addCloud, updateCloud, selectedLayer, selectLayer } = useWeatherV2Store(
    useShallow(s => ({
      cloudLayers: patchCid
        ? (s.draft.patches.find(p => p.client_id === patchCid)?.cloud_layers ?? [])
        : (s.draft.global.cloud_layers ?? []),
      addCloud:      s.addCloud,
      updateCloud:   s.updateCloud,
      selectedLayer: s.selectedLayer,
      selectLayer:   s.selectLayer,
    }))
  )

  const canAdd = cloudLayers.length < MAX_CLOUDS
  const stationElevFt = stationElevM * M_TO_FT

  // Clamp a candidate base_agl_ft so the band stays within [MIN_FT, MAX_FT]
  // MSL and preserves current thickness.
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
        onClick={() => addCloud(patchCid)}
        onTouchEnd={(e) => { e.preventDefault(); if (canAdd) addCloud(patchCid) }}
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
        const isSelected  = selectedLayer?.tabId === activeTab
                         && selectedLayer?.kind === 'cloud'
                         && selectedLayer?.index === i

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
            onMoveAltitude={(newTopFt, newBottomFt) => {
              // Preserve thickness; band moves as a whole. Clamp so top
              // stays ≤ MAX_FT and bottom ≥ MIN_FT MSL.
              const newBaseAgl = clampBaseAgl(
                newBottomFt - stationElevFt,
                thicknessFt,
              )
              updateCloud(i, { base_agl_ft: newBaseAgl }, patchCid)
            }}
            onResizeTop={(newTopFt) => {
              // Bottom stays fixed; only thickness changes.
              const newThicknessFt = Math.max(MIN_THICKNESS_FT, newTopFt - bottomFt)
              const capped = Math.min(newThicknessFt, MAX_FT - bottomFt)
              updateCloud(i, { thickness_m: capped * FT_TO_M }, patchCid)
            }}
            onResizeBottom={(newBotFt) => {
              // Top stays fixed; base_agl + thickness both change.
              const clampedBotFt   = Math.max(MIN_FT, newBotFt)
              const newThicknessFt = Math.max(MIN_THICKNESS_FT, topFt - clampedBotFt)
              const newBaseAgl     = clampedBotFt - stationElevFt
              updateCloud(i, {
                base_agl_ft: newBaseAgl,
                thickness_m: newThicknessFt * FT_TO_M,
              }, patchCid)
            }}
          />
        )
      })}
    </div>
  )
}
