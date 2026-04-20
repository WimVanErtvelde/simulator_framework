import { useState } from 'react'
import { useSimStore } from '../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import NumpadPopup from '../../ui/NumpadPopup'
import { CLOUD_TYPES } from './weatherPresets'
import NonLinearSlider from './graph/NonLinearSlider'
import { MAX_FT, M_TO_FT, FT_TO_M } from './graph/mslScale'

const MIN_THICK_FT = 100

// Left-column editor for the selected cloud layer. All controls mutate
// draft only — no WS until Accept. Base and Top each expose MSL + AGL
// numeric entry (touch-friendly NumpadPopup) plus a sqrt-mapped slider.
// Graph drag, slider, MSL input, and AGL input are all bound to the same
// draft field — edit any one and the others reflow on re-render.
export default function CloudPropertiesEditor({ index, patchContext }) {
  const patchCid = patchContext?.client_id ?? null

  const { globalStationElevM, altFtMsl, altFtAgl } = useSimStore(useShallow(s => ({
    globalStationElevM: s.activeWeather?.stationElevationM ?? 0,
    altFtMsl:           s.fdm?.altFtMsl ?? 0,
    altFtAgl:           s.fdm?.altFtAgl ?? 0,
  })))

  const { cl, updateCloud, removeCloud } = useWeatherV2Store(useShallow(s => ({
    cl: patchCid
      ? s.draft.patches.find(p => p.client_id === patchCid)?.cloud_layers?.[index]
      : s.draft.global.cloud_layers?.[index],
    updateCloud: s.updateCloud,
    removeCloud: s.removeCloud,
  })))

  if (!cl) return null

  // In patch context, station elevation is the patch's ground elevation
  // (AGL refs are patch-relative). In global context, fall back to the
  // weather station elevation from activeWeather.
  const stationElevM = patchContext ? patchContext.stationElevM : globalStationElevM

  const type          = cl.cloud_type ?? 7
  const coverage      = Math.round(cl.coverage_pct ?? 0)

  // Wire stores base_agl_ft and thickness_m. The backend converts
  // base_agl_ft → base_elevation_m using the weather station elevation.
  // When no station is set, we fall back to the aircraft's current ground
  // elevation (altFtMsl − altFtAgl) so the MSL/AGL inputs show sensible
  // different values at airport elevation.
  const stationElevFt = stationElevM * M_TO_FT
  const groundElevFt  = stationElevM > 0
    ? stationElevFt
    : Math.max(0, altFtMsl - altFtAgl)

  // Offset between wire AGL reference (station) and UI AGL reference
  // (ground under aircraft). Zero when station is set; non-zero when
  // falling back to aircraft terrain. Applied so UI AGL reads as
  // "above the ground below me," not "above MSL 0."
  const uiAglOffsetFt = groundElevFt - stationElevFt

  const baseAglWireFt = cl.base_agl_ft ?? 0                       // on the wire
  const thicknessFt   = (cl.thickness_m ?? 0) * M_TO_FT
  const baseMslFt     = Math.round(baseAglWireFt + stationElevFt)
  const topMslFt      = Math.round(baseMslFt + thicknessFt)
  const baseAglUiFt   = Math.round(baseAglWireFt - uiAglOffsetFt) // displayed
  const topAglUiFt    = Math.round(baseAglUiFt + thicknessFt)

  // ── Mutators ───────────────────────────────────────────────────────────
  // MSL edits — translate to wire AGL + thickness.
  const setBaseMsl = (newBaseMsl) => {
    const newBaseAgl = Math.max(0, newBaseMsl - stationElevFt)
    const maxThicknessFt = MAX_FT - (newBaseAgl + stationElevFt)
    const newThicknessFt = Math.max(MIN_THICK_FT, Math.min(thicknessFt, maxThicknessFt))
    updateCloud(index, {
      base_agl_ft: newBaseAgl,
      thickness_m: newThicknessFt * FT_TO_M,
    }, patchCid)
  }
  const setTopMsl = (newTopMsl) => {
    const newThicknessFt = Math.max(MIN_THICK_FT, newTopMsl - baseMslFt)
    const capped = Math.min(newThicknessFt, MAX_FT - baseMslFt)
    updateCloud(index, { thickness_m: capped * FT_TO_M }, patchCid)
  }

  // AGL edits — UI AGL is relative to ground elevation. Translate to
  // MSL first, then to wire AGL (which is relative to station).
  const setBaseAgl = (newUiAgl) => {
    const newMsl = Math.max(0, newUiAgl) + groundElevFt
    setBaseMsl(newMsl)
  }
  const setTopAgl = (newUiAgl) => {
    const newMsl = Math.max(0, newUiAgl) + groundElevFt
    setTopMsl(newMsl)
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
                onClick={() => updateCloud(index, { cloud_type: t.id }, patchCid)}
                onTouchEnd={(e) => { e.preventDefault(); updateCloud(index, { cloud_type: t.id }, patchCid) }}
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
          onChange={(e) => updateCloud(index, { coverage_pct: Number(e.target.value) }, patchCid)}
          style={{
            width: '100%', accentColor: '#39d0d8',
            cursor: 'pointer', touchAction: 'manipulation',
          }}
        />
      </div>

      {/* Base — MSL + AGL inputs on one row, slider below */}
      <AltitudeField
        label="Base"
        mslFt={baseMslFt}
        aglFt={baseAglUiFt}
        minMslFt={Math.round(stationElevFt)}
        maxMslFt={Math.max(Math.round(stationElevFt) + MIN_THICK_FT, topMslFt - MIN_THICK_FT)}
        onSubmitMsl={setBaseMsl}
        onSubmitAgl={setBaseAgl}
      />

      {/* Top — MSL + AGL inputs on one row, thickness shown inline for
          reference, slider below */}
      <AltitudeField
        label="Top"
        detail={`thickness: ${Math.round(thicknessFt).toLocaleString()} ft`}
        mslFt={topMslFt}
        aglFt={topAglUiFt}
        minMslFt={baseMslFt + MIN_THICK_FT}
        maxMslFt={MAX_FT}
        onSubmitMsl={setTopMsl}
        onSubmitAgl={setTopAgl}
      />

      <button
        type="button"
        onClick={() => removeCloud(index, patchCid)}
        onTouchEnd={(e) => { e.preventDefault(); removeCloud(index, patchCid) }}
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

// One altitude row: "BASE" / "TOP" label, MSL numpad field, AGL numpad
// field, and a sqrt-mapped slider (MSL-driven). All four controls
// reflow through the same draft field — edit any one and the others
// update on re-render.
function AltitudeField({ label, detail, mslFt, aglFt, minMslFt, maxMslFt,
                        onSubmitMsl, onSubmitAgl }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
      <div style={{
        display: 'flex', alignItems: 'center',
        gap: 6, flexWrap: 'wrap',
        fontSize: 11, fontFamily: 'monospace',
      }}>
        <span style={{
          color: '#64748b', letterSpacing: 1, textTransform: 'uppercase',
          flex: '0 0 auto', minWidth: 36,
        }}>{label}</span>
        <span style={{
          color: '#94a3b8', fontSize: 11,
          flex: '1 1 auto', minWidth: 0,
          overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap',
        }}>{detail}</span>
        <NumpadField
          label={`${label} MSL`}
          unit="MSL"
          valueFt={mslFt}
          onSubmit={onSubmitMsl}
        />
        <NumpadField
          label={`${label} AGL`}
          unit="AGL"
          valueFt={aglFt}
          onSubmit={onSubmitAgl}
        />
      </div>
      <NonLinearSlider
        valueFt={mslFt}
        minFt={minMslFt} maxFt={maxMslFt}
        onChange={onSubmitMsl}
      />
    </div>
  )
}

// Click-to-open numpad input — value is an integer ft, rendered
// right-aligned with a trailing unit badge. Opens NumpadPopup on click;
// submits an integer back to onSubmit.
function NumpadField({ label, unit, valueFt, onSubmit }) {
  const [open, setOpen] = useState(false)
  return (
    <>
      <button
        type="button"
        onClick={() => setOpen(true)}
        onTouchEnd={(e) => { e.preventDefault(); setOpen(true) }}
        style={{
          height: 22, padding: '0 6px',
          background: '#1c2333',
          border: '1px solid #1e293b',
          borderRadius: 2,
          color: '#e2e8f0',
          fontFamily: 'monospace', fontSize: 12,
          fontVariantNumeric: 'tabular-nums',
          display: 'flex', alignItems: 'center', gap: 4,
          cursor: 'pointer', touchAction: 'manipulation',
        }}
      >
        <span style={{ flex: '0 1 auto', textAlign: 'right', minWidth: 40 }}>
          {Number(valueFt).toLocaleString()}
        </span>
        <span style={{ fontSize: 9, color: '#64748b', letterSpacing: 1 }}>{unit}</span>
      </button>
      {open && (
        <NumpadPopup
          label={label}
          hint="ft"
          value={String(Math.round(valueFt))}
          allowDecimal={false}
          onSubmit={(v) => {
            const n = Number(v)
            if (Number.isFinite(n)) onSubmit(n)
            setOpen(false)
          }}
          onCancel={() => setOpen(false)}
        />
      )}
    </>
  )
}
