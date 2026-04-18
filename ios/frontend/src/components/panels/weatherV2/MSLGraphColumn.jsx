// Middle column of WeatherPanelV2 Global tab.
//
// Populated in 5a-iii/iv: vertical MSL altitude axis (0-50000 ft) with cloud
// and wind layer cards positioned by their authored altitude.
// + Cloud Layer / + Wind Layer add buttons at the top. X-Plane 12 pattern.
//
// Currently a placeholder showing only the axis ticks and disabled add buttons.

const TICKS = [50000, 40000, 30000, 20000, 10000, 0]

const addBtn = {
  height: 32, padding: '0 12px',
  background: '#111827', border: '1px solid #1e293b', borderRadius: 3,
  color: '#475569', fontSize: 11, fontFamily: 'monospace',
  fontWeight: 700, letterSpacing: 1, textTransform: 'uppercase',
  cursor: 'not-allowed', opacity: 0.6,
}

export default function MSLGraphColumn() {
  return (
    <div style={{
      padding: 12,
      background: '#0d1117',
      border: '1px solid #1e293b',
      borderRadius: 3,
      color: '#64748b',
      fontFamily: 'monospace',
      fontSize: 12,
      height: '100%',
      minHeight: 0,
      display: 'flex', flexDirection: 'column',
    }}>
      <div style={{ display: 'flex', gap: 8, marginBottom: 12 }}>
        <button type="button" disabled style={addBtn}>+ Cloud Layer</button>
        <button type="button" disabled style={addBtn}>+ Wind Layer</button>
      </div>

      <div style={{
        flex: 1, position: 'relative', minHeight: 240,
        borderLeft: '1px solid #1e293b',
        marginLeft: 56, paddingLeft: 8,
      }}>
        {TICKS.map((alt, i) => (
          <div key={alt} style={{
            position: 'absolute',
            top: `${(i / (TICKS.length - 1)) * 100}%`,
            left: -64, right: 0,
            display: 'flex', alignItems: 'center', gap: 8,
            transform: 'translateY(-50%)',
          }}>
            <span style={{
              width: 56, textAlign: 'right',
              fontSize: 10, color: '#475569',
              fontFamily: 'monospace', fontVariantNumeric: 'tabular-nums',
            }}>
              {alt.toLocaleString()} ft
            </span>
            <div style={{ flex: 1, height: 1, background: '#1e293b' }} />
          </div>
        ))}
      </div>

      <div style={{ marginTop: 12, fontSize: 10, color: '#475569', fontStyle: 'italic' }}>
        Populated in Slice 5a-iii (clouds) / 5a-iv (winds).
      </div>
    </div>
  )
}
