import { useSimStore } from '../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import { CLOUD_TYPES } from './weatherPresets'
import NonLinearSlider from './graph/NonLinearSlider'
import { MAX_FT, M_TO_FT, FT_TO_M } from './graph/mslScale'

const MIN_THICK_FT = 100

// Left-column editor for the selected cloud layer. All controls mutate
// draft only — no WS until Accept. Base MSL and Top MSL each pair a
// numeric input with a NonLinearSlider; combined with the graph drag
// they are three-way-bound to the same draft field.
export default function CloudPropertiesEditor({ index }) {
  const stationElevM = useSimStore(s => s.activeWeather?.stationElevationM ?? 0)

  const { cl, updateCloud, removeCloud } = useWeatherV2Store(useShallow(s => ({
    cl:          s.draft.global.cloud_layers?.[index],
    updateCloud: s.updateCloud,
    removeCloud: s.removeCloud,
  })))

  if (!cl) return null

  const type          = cl.cloud_type ?? 7
  const coverage      = Math.round(cl.coverage_pct ?? 0)
  const baseAglFt     = cl.base_agl_ft ?? 0
  const thicknessFt   = (cl.thickness_m ?? 0) * M_TO_FT
  const stationElevFt = stationElevM * M_TO_FT
  const baseMslFt     = Math.round(baseAglFt + stationElevFt)
  const topMslFt      = Math.round(baseMslFt + thicknessFt)

  // Mutators — translate MSL edits back into AGL+thickness storage.
  const setBaseMsl = (newBaseMsl) => {
    const newBaseAgl = Math.max(0, newBaseMsl - stationElevFt)
    // Preserve thickness, but shrink if new base would push top past MAX_FT.
    const maxThicknessFt = MAX_FT - (newBaseAgl + stationElevFt)
    const newThicknessFt = Math.max(MIN_THICK_FT, Math.min(thicknessFt, maxThicknessFt))
    updateCloud(index, {
      base_agl_ft: newBaseAgl,
      thickness_m: newThicknessFt * FT_TO_M,
    })
  }
  const setTopMsl = (newTopMsl) => {
    const newThicknessFt = Math.max(MIN_THICK_FT, newTopMsl - baseMslFt)
    const capped = Math.min(newThicknessFt, MAX_FT - baseMslFt)
    updateCloud(index, { thickness_m: capped * FT_TO_M })
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
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
                onClick={() => updateCloud(index, { cloud_type: t.id })}
                onTouchEnd={(e) => { e.preventDefault(); updateCloud(index, { cloud_type: t.id }) }}
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
          onChange={(e) => updateCloud(index, { coverage_pct: Number(e.target.value) })}
          style={{
            width: '100%', accentColor: '#39d0d8',
            cursor: 'pointer', touchAction: 'manipulation',
          }}
        />
      </div>

      {/* Base MSL — numeric input + sqrt-mapped slider */}
      <AltitudeField
        label="Base"
        valueFt={baseMslFt}
        minFt={Math.round(stationElevFt)}
        maxFt={Math.max(Math.round(stationElevFt) + MIN_THICK_FT, topMslFt - MIN_THICK_FT)}
        onChange={setBaseMsl}
      />

      {/* Top MSL — numeric input + sqrt-mapped slider */}
      <AltitudeField
        label="Top"
        valueFt={topMslFt}
        minFt={baseMslFt + MIN_THICK_FT}
        maxFt={MAX_FT}
        onChange={setTopMsl}
      />

      <div style={{
        fontSize: 10, color: '#475569', fontFamily: 'monospace',
        fontStyle: 'italic',
      }}>
        AGL base {Math.round(baseAglFt).toLocaleString()} ft · thickness {Math.round(thicknessFt).toLocaleString()} ft
      </div>

      <button
        type="button"
        onClick={() => removeCloud(index)}
        onTouchEnd={(e) => { e.preventDefault(); removeCloud(index) }}
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

// Numeric input + NonLinearSlider for one altitude value. Three-way-bound
// to the same draft field: graph drag / slider / input all call onChange
// with ft MSL; the parent writes it to draft; valueFt flows back in on
// re-render.
function AltitudeField({ label, valueFt, minFt, maxFt, onChange }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between',
                    alignItems: 'center',
                    fontSize: 11, fontFamily: 'monospace' }}>
        <span style={{ color: '#64748b', letterSpacing: 1, textTransform: 'uppercase' }}>
          {label} (ft MSL)
        </span>
        <input
          type="number"
          value={valueFt}
          min={minFt} max={maxFt} step={50}
          onChange={(e) => {
            const v = Number(e.target.value)
            if (Number.isFinite(v)) onChange(Math.max(minFt, Math.min(maxFt, v)))
          }}
          style={{
            width: 88, height: 22, padding: '0 6px',
            background: '#1c2333',
            border: '1px solid #1e293b',
            borderRadius: 2,
            color: '#e2e8f0',
            fontFamily: 'monospace', fontSize: 12,
            fontVariantNumeric: 'tabular-nums',
            textAlign: 'right',
          }}
        />
      </div>
      <NonLinearSlider
        valueFt={valueFt}
        minFt={minFt} maxFt={maxFt}
        onChange={onChange}
      />
    </div>
  )
}
