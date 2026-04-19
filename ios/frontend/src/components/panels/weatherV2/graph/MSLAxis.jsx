import { useSimStore } from '../../../../store/useSimStore'
import { axisTicks, ftToY } from './mslScale'

// Fixed MSL axis width (label column).
export const AXIS_WIDTH  = 60
const TICK_LENGTH = 6

// Vertical altitude axis with tick labels and an ownship marker.
// Ownship altitude comes from useSimStore.fdm.altFtMsl (already in feet).
export default function MSLAxis({ height }) {
  const altFtMsl = useSimStore(s => s.fdm?.altFtMsl ?? 0)
  const ticks = axisTicks(5000)
  const ownY  = ftToY(altFtMsl, height)

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

      {/* Ownship altitude marker (▶ pointing right into the graph) */}
      <div style={{
        position: 'absolute',
        top: ownY, right: -6,
        transform: 'translateY(-50%)',
        zIndex: 10,
        fontSize: 12, color: '#bc4fcb',
        fontWeight: 700, fontFamily: 'monospace',
        textShadow: '0 0 4px rgba(0,0,0,0.7)',
        pointerEvents: 'none',
      }}>▶</div>
    </div>
  )
}
