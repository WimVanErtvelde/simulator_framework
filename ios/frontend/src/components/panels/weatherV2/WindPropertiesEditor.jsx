import { useState } from 'react'
import { useShallow } from 'zustand/react/shallow'
import { useSimStore } from '../../../store/useSimStore'
import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import CompassDial from './graph/CompassDial'
import NonLinearSlider from './graph/NonLinearSlider'
import NumpadPopup from '../../ui/NumpadPopup'
import { MAX_FT, MIN_FT, M_TO_FT, FT_TO_M } from './graph/mslScale'

const KT_TO_MS = 0.514444
const MS_TO_KT = 1 / KT_TO_MS
const MIN_ALT_FT = MIN_FT
const MAX_ALT_FT = MAX_FT

// Left-column editor for the selected wind layer. All controls mutate
// draft only — no WS until Accept. Altitude uses a NumpadField + sqrt
// slider; direction combines CompassDial + NumpadField + T/M toggle with
// the reciprocal mode shown as a reference line.
export default function WindPropertiesEditor({ index }) {
  const magVarDeg = useSimStore(s => s.nav?.magVariationDeg ?? 0)

  const { wl, updateWind, removeWind } = useWeatherV2Store(useShallow(s => ({
    wl:         s.draft.global.wind_layers?.[index],
    updateWind: s.updateWind,
    removeWind: s.removeWind,
  })))

  const [dirMode, setDirMode] = useState('true')   // 'true' | 'mag'

  if (!wl) return null

  const altMslFt   = Math.round((wl.altitude_msl_m ?? 0) * M_TO_FT)
  const trueDeg    = Math.round(wl.wind_direction_deg ?? 0)
  const magDeg     = (((trueDeg - magVarDeg) % 360) + 360) % 360
  const spdKt      = Math.round((wl.wind_speed_ms ?? 0) * MS_TO_KT)
  // Display gust value clamped to >= sustained so the UI never shows a
  // nonsensical "gust below sustained" state even if the stored value
  // briefly lags (e.g. after a sustained bump, before setSpdKt pull-up).
  const gustKt     = Math.max(spdKt, Math.round((wl.gust_speed_ms ?? 0) * MS_TO_KT))
  const verticalMs = wl.vertical_wind_ms ?? 0
  const turbPct    = Math.round((wl.turbulence_severity ?? 0) * 100)

  const displayDeg = dirMode === 'true' ? trueDeg : Math.round(magDeg)

  const setTrueDeg = (newTrue) => {
    const wrapped = ((Number(newTrue) % 360) + 360) % 360
    updateWind(index, { wind_direction_deg: wrapped })
  }
  const setDisplayDeg = (newDisp) => {
    // Storage is always in true. Convert if user entered magnetic.
    const newTrue = dirMode === 'true' ? newDisp : (newDisp + magVarDeg)
    setTrueDeg(newTrue)
  }
  const setAltMslFt = (ft) => {
    const clamped = Math.max(MIN_ALT_FT, Math.min(MAX_ALT_FT, ft))
    updateWind(index, { altitude_msl_m: clamped * FT_TO_M })
  }
  const setSpdKt = (kt) => {
    const newSustainedMs = Math.max(0, kt) * KT_TO_MS
    const curGustMs = wl.gust_speed_ms ?? 0
    const patch = { wind_speed_ms: newSustainedMs }
    // If sustained now exceeds stored gust, pull gust up to match so the
    // "gust >= sustained" invariant is always satisfied.
    if (curGustMs < newSustainedMs) patch.gust_speed_ms = newSustainedMs
    updateWind(index, patch)
  }
  const setGustKt = (kt) => {
    // GUST slider min is bound to spdKt in the UI, but double-clamp here
    // in case callers (e.g. sustained pull-up above) pass a lower value.
    const clampedKt = Math.max(spdKt, kt)
    updateWind(index, { gust_speed_ms: clampedKt * KT_TO_MS })
  }
  const setVerticalMs = (ms) => updateWind(index, { vertical_wind_ms: ms })
  const setTurbPct    = (pct) => updateWind(index, {
    turbulence_severity: Math.max(0, Math.min(1, pct / 100)),
  })

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
      <div style={{
        fontSize: 11, fontWeight: 700, letterSpacing: 1,
        color: '#e2e8f0', textTransform: 'uppercase',
        fontFamily: 'monospace',
      }}>Wind Layer {index + 1}</div>

      {/* Altitude */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <div style={fieldRow}>
          <span style={fieldLabel}>Altitude</span>
          <NumpadField
            label="Altitude MSL"
            unit="MSL"
            valueFt={altMslFt}
            onSubmit={setAltMslFt}
          />
        </div>
        <NonLinearSlider
          valueFt={altMslFt}
          minFt={MIN_ALT_FT} maxFt={MAX_ALT_FT}
          onChange={setAltMslFt}
        />
      </div>

      {/* Direction */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        <div style={fieldRow}>
          <span style={fieldLabel}>Direction (from)</span>
          <div style={{ display: 'flex' }}>
            <button type="button"
              onClick={() => setDirMode('true')}
              style={pillBtn(dirMode === 'true', 'left')}>T</button>
            <button type="button"
              onClick={() => setDirMode('mag')}
              style={pillBtn(dirMode === 'mag', 'right')}>M</button>
          </div>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 12,
                      justifyContent: 'center' }}>
          <CompassDial
            valueDeg={trueDeg}
            onChange={setTrueDeg}
            size={108}
            indicatorColor="#a78bfa"
          />
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, alignItems: 'center' }}>
            <NumpadField
              label={`Direction ${dirMode === 'true' ? 'True' : 'Magnetic'}`}
              unit={dirMode === 'true' ? '°T' : '°M'}
              valueFt={displayDeg}
              onSubmit={setDisplayDeg}
              wrap360
            />
            <span style={{
              fontSize: 10, color: '#64748b',
              fontFamily: 'monospace',
            }}>
              {dirMode === 'true'
                ? `M: ${String(Math.round(magDeg)).padStart(3, '0')}°`
                : `T: ${String(trueDeg).padStart(3, '0')}°`}
            </span>
          </div>
        </div>
      </div>

      {/* Speed */}
      <ValueSliderField
        label="Speed"
        value={spdKt} min={0} max={200} step={1}
        suffix=" kt"
        onChange={setSpdKt}
      />

      {/* Gust peak — constrained >= sustained */}
      <ValueSliderField
        label="Gust"
        value={gustKt} min={spdKt} max={200} step={1}
        suffix=" kt"
        onChange={setGustKt}
      />

      {/* Vertical */}
      <ValueSliderField
        label="Vertical"
        value={verticalMs} min={-30} max={30} step={0.5}
        suffix=" m/s"
        format={(v) => v.toFixed(1)}
        onChange={setVerticalMs}
      />

      {/* Turbulence */}
      <ValueSliderField
        label="Turbulence"
        value={turbPct} min={0} max={100} step={5}
        suffix=" %"
        onChange={setTurbPct}
      />

      <button
        type="button"
        onClick={() => removeWind(index)}
        onTouchEnd={(e) => { e.preventDefault(); removeWind(index) }}
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

// ── Local helpers ─────────────────────────────────────────────────────────

function ValueSliderField({ label, value, min, max, step, suffix, format, onChange }) {
  const shown = format ? format(value) : String(Math.round(value))
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
      <div style={fieldRow}>
        <span style={fieldLabel}>{label}</span>
        <span style={{ color: '#e2e8f0', fontSize: 12, fontFamily: 'monospace',
                       fontVariantNumeric: 'tabular-nums' }}>
          {shown}{suffix}
        </span>
      </div>
      <input
        type="range"
        min={min} max={max} step={step}
        value={value}
        onChange={(e) => onChange(Number(e.target.value))}
        style={{ width: '100%', accentColor: '#a78bfa',
                 cursor: 'pointer', touchAction: 'manipulation' }}
      />
    </div>
  )
}

// Click-to-open numpad input for any integer quantity. When wrap360 is
// set, submitted values are wrapped into [0, 360) before propagation.
function NumpadField({ label, unit, valueFt, onSubmit, wrap360 = false }) {
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
          value={String(Math.round(valueFt))}
          allowDecimal={false}
          onSubmit={(v) => {
            let n = Number(v)
            if (Number.isFinite(n)) {
              if (wrap360) n = ((n % 360) + 360) % 360
              onSubmit(n)
            }
            setOpen(false)
          }}
          onCancel={() => setOpen(false)}
        />
      )}
    </>
  )
}

const fieldRow = {
  display: 'flex', alignItems: 'center', justifyContent: 'space-between',
  gap: 6, fontSize: 11, fontFamily: 'monospace',
}
const fieldLabel = {
  color: '#64748b', letterSpacing: 1, textTransform: 'uppercase',
}
function pillBtn(active, side) {
  return {
    height: 22, padding: '0 10px',
    background: active ? 'rgba(167, 139, 250, 0.10)' : '#111827',
    border: `1px solid ${active ? '#a78bfa' : '#1e293b'}`,
    borderRadius: side === 'left' ? '3px 0 0 3px' : '0 3px 3px 0',
    color: active ? '#a78bfa' : '#64748b',
    fontSize: 10, fontFamily: 'monospace', fontWeight: 700,
    letterSpacing: 1, cursor: 'pointer',
    touchAction: 'manipulation',
  }
}
