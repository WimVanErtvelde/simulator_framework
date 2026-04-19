import { useSimStore } from '../../../../store/useSimStore'
import { axisTicks, ftToY, M_TO_FT } from './mslScale'

// Fixed MSL axis width (label column).
export const AXIS_WIDTH  = 60
const TICK_LENGTH = 6

// Vertical altitude axis with tick labels and an ownship readout.
// Ownship altitude comes from useSimStore.fdm.altFtMsl (already in feet).
// Station elevation (from activeWeather) drives the AGL secondary line.
export default function MSLAxis({ height }) {
  const altFtMsl     = useSimStore(s => s.fdm?.altFtMsl ?? 0)
  const stationElevM = useSimStore(s => s.activeWeather?.stationElevationM ?? 0)

  const ticks = axisTicks()
  const ownY  = ftToY(altFtMsl, height)

  // MSL line: FL above 18000 ft, absolute feet below.
  const mslText = altFtMsl >= 18000
    ? `FL${String(Math.round(altFtMsl / 100)).padStart(3, '0')}`
    : `${Math.round(altFtMsl).toLocaleString()} ft MSL`

  // AGL only meaningful when a station elevation is set.
  const aglFt   = altFtMsl - stationElevM * M_TO_FT
  const showAgl = stationElevM > 0

  return (
    <div style={{
      position: 'relative',
      width: AXIS_WIDTH,
      height,
      flexShrink: 0,
      fontFamily: 'monospace',
    }}>
      {/* Axis line */}
      <div style={{
        position: 'absolute',
        top: 0, right: 0, bottom: 0, width: 1,
        background: '#1e293b',
      }} />

      {/* Tick marks + labels */}
      {ticks.map(alt => {
        const y = ftToY(alt, height)
        return (
          <div key={alt} style={{
            position: 'absolute',
            top: y, right: 0,
            display: 'flex', alignItems: 'center',
            transform: 'translateY(-50%)',
          }}>
            <span style={{
              fontSize: 10, color: '#475569',
              paddingRight: 6, textAlign: 'right',
              minWidth: AXIS_WIDTH - TICK_LENGTH - 6,
              fontVariantNumeric: 'tabular-nums',
            }}>
              {alt === 0 ? '0 ft' : `${(alt / 1000)}k`}
            </span>
            <div style={{ width: TICK_LENGTH, height: 1, background: '#1e293b' }} />
          </div>
        )
      })}

      {/* Ownship marker + MSL/AGL readout */}
      <div style={{
        position: 'absolute',
        top: ownY, right: -6,
        transform: 'translateY(-50%)',
        zIndex: 10,
        display: 'flex', alignItems: 'center', gap: 4,
        pointerEvents: 'none',
      }}>
        <span style={{
          fontSize: 12, color: '#bc4fcb',
          fontWeight: 700,
          textShadow: '0 0 4px rgba(0,0,0,0.7)',
        }}>▶</span>
        <div style={{
          display: 'flex', flexDirection: 'column',
          fontSize: 9, lineHeight: 1.2,
          fontFamily: 'monospace', fontVariantNumeric: 'tabular-nums',
          color: '#bc4fcb',
          background: 'rgba(11, 15, 25, 0.85)',
          padding: '2px 6px',
          borderRadius: 2,
          border: '1px solid rgba(188, 79, 203, 0.3)',
          whiteSpace: 'nowrap',
        }}>
          <span>{mslText}</span>
          {showAgl && (
            <span style={{ color: '#a78bfa' }}>
              {Math.round(aglFt).toLocaleString()} ft AGL
            </span>
          )}
        </div>
      </div>
    </div>
  )
}
