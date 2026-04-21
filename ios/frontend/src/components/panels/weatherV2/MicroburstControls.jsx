// Shared microburst authoring UI. Used by PatchMicroburstSection
// inside each patch tab. (Formerly also used by a standalone
// MicroburstTab, retired in the 5c followup cleanup.)
//
// Intensity preset → peak outflow lambda (m/s). Matches V1 INTENSITY_MAP.
export const INTENSITY_MS = { Mild: 5.0, Med: 10.0, Sev: 15.0 }

// Reverse lookup for decoding server-stored intensity back to a preset.
// Non-preset values (rare — only from V1 authoring or scenario load) fall
// through to 'Med'.
export function intensityLabelFor(ms) {
  if (ms <= 7.5)  return 'Mild'
  if (ms >= 12.5) return 'Sev'
  return 'Med'
}

export default function MicroburstControls({
  intensity, onIntensityChange,
  diameterM, onDiameterChange,
  distanceNm, onDistanceChange,
  showDistance,
  bearingDeg, onBearingChange,
  showBearing,
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
      <div>
        <div style={fieldLabel}>INTENSITY</div>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 4 }}>
          {['Mild', 'Med', 'Sev'].map(level => (
            <IntensityButton key={level} level={level}
              active={intensity === level}
              onClick={() => onIntensityChange(level)} />
          ))}
        </div>
      </div>

      <div>
        <div style={fieldLabel}>DIAMETER · {diameterM} m</div>
        <input type="range" min={200} max={5000} step={100}
          value={diameterM}
          onChange={e => onDiameterChange(Number(e.target.value))}
          style={sliderStyle}
        />
      </div>

      {showDistance && (
        <div>
          <div style={fieldLabel}>DISTANCE FROM THRESHOLD · {distanceNm.toFixed(1)} NM</div>
          <input type="range" min={-3} max={10} step={0.5}
            value={distanceNm}
            onChange={e => onDistanceChange(Number(e.target.value))}
            style={sliderStyle}
          />
          <div style={hintStyle}>
            positive = on approach · negative = past threshold
          </div>
        </div>
      )}

      {showBearing && (
        <>
          <div>
            <div style={fieldLabel}>BEARING FROM CENTER · {bearingDeg}°</div>
            <input type="range" min={0} max={359} step={1}
              value={bearingDeg}
              onChange={e => onBearingChange(Number(e.target.value))}
              style={sliderStyle}
            />
          </div>
          <div>
            <div style={fieldLabel}>DISTANCE FROM CENTER · {distanceNm.toFixed(1)} NM</div>
            <input type="range" min={0} max={15} step={0.5}
              value={distanceNm}
              onChange={e => onDistanceChange(Number(e.target.value))}
              style={sliderStyle}
            />
          </div>
        </>
      )}
    </div>
  )
}

function IntensityButton({ level, active, onClick }) {
  return (
    <button
      type="button"
      onClick={onClick}
      onTouchEnd={(e) => { e.preventDefault(); onClick() }}
      style={{
        padding: '8px 0',
        fontFamily: 'monospace',
        fontSize: 11, fontWeight: 700,
        letterSpacing: 1, textTransform: 'uppercase',
        border: `1px solid ${active ? '#bc4fcb' : '#1e293b'}`,
        background: active ? 'rgba(188, 79, 203, 0.12)' : '#111827',
        color: active ? '#bc4fcb' : '#94a3b8',
        borderRadius: 3,
        cursor: 'pointer',
        touchAction: 'manipulation',
      }}
    >{level}</button>
  )
}

const fieldLabel = {
  fontFamily: 'monospace',
  fontSize: 10, fontWeight: 700,
  letterSpacing: 1,
  color: '#64748b',
  marginBottom: 4,
  textTransform: 'uppercase',
}

const sliderStyle = {
  width: '100%',
  accentColor: '#39d0d8',
}

const hintStyle = {
  fontFamily: 'monospace',
  fontSize: 9, color: '#475569',
  fontStyle: 'italic',
  marginTop: 3,
}
