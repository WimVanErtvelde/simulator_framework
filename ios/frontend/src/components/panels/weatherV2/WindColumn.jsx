import { useShallow } from 'zustand/react/shallow'
import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import LayerBand from './graph/LayerBand'
import { MAX_FT, MIN_FT, M_TO_FT, FT_TO_M } from './graph/mslScale'

const MAX_WINDS    = 13      // X-Plane WeatherState hard cap
const WIND_BAR_FT  = 150     // visual half-height around the point altitude
const KT_TO_MS     = 0.514444
const MS_TO_KT     = 1 / KT_TO_MS

// Right-middle column that hosts wind layers on the MSL graph. Wind is a
// point-altitude value — we render it as a thin horizontal band centered
// at altitude_msl_m ± WIND_BAR_FT. LayerBand.detectEdge returns 'body'
// for kind='wind', so there's no top/bottom resize; body-drag moves the
// altitude. Parallel to CloudColumn; reads draft wind_layers and
// dispatches store actions only — no WS until Accept.
export default function WindColumn({ height, width }) {
  const { windLayers, addWind, updateWind, selectedLayer, selectLayer } =
    useWeatherV2Store(useShallow(s => ({
      windLayers:    s.draft.global.wind_layers ?? [],
      addWind:       s.addWind,
      updateWind:    s.updateWind,
      selectedLayer: s.selectedLayer,
      selectLayer:   s.selectLayer,
    })))

  const canAdd = windLayers.length < MAX_WINDS

  return (
    <div style={{
      position: 'relative',
      width,
      height,
      borderLeft: '1px dashed #1e293b',
      background: '#050810',
      flexShrink: 0,
    }}>
      <button
        type="button"
        disabled={!canAdd}
        onClick={addWind}
        onTouchEnd={(e) => { e.preventDefault(); if (canAdd) addWind() }}
        style={{
          position: 'absolute',
          top: -36, left: 0,
          height: 28, padding: '0 12px',
          background: canAdd ? '#111827' : '#0b1220',
          border: `1px solid ${canAdd ? '#1e293b' : '#111827'}`,
          borderRadius: 3,
          color: canAdd ? '#a78bfa' : '#334155',
          fontFamily: 'monospace', fontSize: 11, fontWeight: 700,
          letterSpacing: 1, textTransform: 'uppercase',
          cursor: canAdd ? 'pointer' : 'not-allowed',
          touchAction: 'manipulation',
        }}
      >+ Wind Layer</button>

      {windLayers.map((wl, i) => {
        const altMslFt = (wl.altitude_msl_m ?? 0) * M_TO_FT
        const dirDeg   = Math.round(wl.wind_direction_deg ?? 0)
        const spdKt    = Math.round((wl.wind_speed_ms ?? 0) * MS_TO_KT)
        const label    = `${String(dirDeg).padStart(3, '0')}° / ${spdKt} kt`
        const isSelected = selectedLayer?.kind === 'wind' && selectedLayer?.index === i

        return (
          <LayerBand
            key={i}
            kind="wind"
            topFt={altMslFt + WIND_BAR_FT}
            bottomFt={altMslFt - WIND_BAR_FT}
            height={height}
            width={width}
            selected={isSelected}
            label={label}
            color="#a78bfa"      /* purple — distinct from cloud teal/pink */
            onSelect={() => selectLayer('wind', i)}
            onMoveAltitude={(newTopFt, newBottomFt) => {
              // Point-altitude: midpoint of the band is the new altitude.
              const midFt = (newTopFt + newBottomFt) / 2
              const clamped = Math.max(MIN_FT, Math.min(MAX_FT, midFt))
              updateWind(i, { altitude_msl_m: clamped * FT_TO_M })
            }}
            /* Wind bands have no top/bottom resize — detectEdge already
               returns 'body' for kind='wind', so onResizeTop/Bottom are
               never invoked and we don't pass them. */
          />
        )
      })}
    </div>
  )
}
