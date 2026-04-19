import { useCallback, useRef } from 'react'
import { ftToY, yToFt } from './mslScale'

const EDGE_HIT_PX   = 8      // drag-resize sensitivity near top/bottom edges
const MIN_BAND_PX   = 12     // minimum rendered band height for clouds
const MIN_WIND_PX   = 3      // point-altitude bar for winds
const MIN_THICK_FT  = 100    // minimum thickness while resizing

// Draggable layer band on the MSL graph.
//
// Drag fires callbacks on every pointer-move with ABSOLUTE ft MSL values.
// Start values are frozen in startRef at pointer-down, so cumulative delta
// is always computed against the original band — no double-accumulation
// when draft mutation round-trips through props.
//
// Props:
//   kind      : 'cloud' | 'wind' — affects visual + whether edges resize
//   topFt     : top altitude MSL in ft (higher altitude)
//   bottomFt  : bottom altitude MSL in ft (lower altitude) (same as topFt for wind)
//   height    : pixel height of the graph viewport
//   width     : pixel width of the band column
//   selected  : boolean — highlight state
//   label     : string displayed in the band
//   color     : base color for the band
//   onSelect  : () => void (fires on pointer-down)
//   onMoveAltitude(newTopFt, newBottomFt) : fires during body-drag
//   onResizeTop(newTopFt)                 : fires during top-edge drag (cloud only)
//   onResizeBottom(newBotFt)              : fires during bottom-edge drag (cloud only)
export default function LayerBand({
  kind,
  topFt,
  bottomFt,
  height,
  width,
  selected,
  label,
  color,
  onSelect,
  onMoveAltitude,
  onResizeTop,
  onResizeBottom,
}) {
  const ref = useRef(null)
  // { edge, startTopFt, startBottomFt, startPointerFt }
  const dragRef = useRef(null)

  const detectEdge = (clientY) => {
    if (kind === 'wind') return 'body'
    const rect = ref.current.getBoundingClientRect()
    const localY = clientY - rect.top
    if (localY <= EDGE_HIT_PX)               return 'top'
    if (localY >= rect.height - EDGE_HIT_PX) return 'bottom'
    return 'body'
  }

  // Convert a clientY to ft MSL inside the graph column's coordinate
  // space. Band is absolutely positioned inside the graph host; graph
  // host rect covers Y=0 (MAX_FT) to Y=height (0 ft).
  const pointerToFt = (clientY) => {
    const host = ref.current?.parentElement
    if (!host) return 0
    const hostRect = host.getBoundingClientRect()
    return yToFt(clientY - hostRect.top, height)
  }

  // Callbacks intentionally omit pointerToFt / detectEdge from deps — those
  // are stable closures over `ref` (itself stable) + `height` which IS in
  // the dep list.
  /* eslint-disable react-hooks/exhaustive-deps */
  const onPointerDown = useCallback((e) => {
    e.stopPropagation()
    if (onSelect) onSelect()
    e.currentTarget.setPointerCapture(e.pointerId)
    dragRef.current = {
      edge:            detectEdge(e.clientY),
      startTopFt:      topFt,
      startBottomFt:   bottomFt,
      startPointerFt:  pointerToFt(e.clientY),
    }
  }, [onSelect, topFt, bottomFt, kind, height])

  const onPointerMove = useCallback((e) => {
    const d = dragRef.current
    if (!d) return
    const deltaFt = pointerToFt(e.clientY) - d.startPointerFt

    if (d.edge === 'body' && onMoveAltitude) {
      onMoveAltitude(d.startTopFt + deltaFt, d.startBottomFt + deltaFt)
    } else if (d.edge === 'top' && onResizeTop) {
      const newTopFt = Math.max(d.startBottomFt + MIN_THICK_FT, d.startTopFt + deltaFt)
      onResizeTop(newTopFt)
    } else if (d.edge === 'bottom' && onResizeBottom) {
      const newBotFt = Math.min(d.startTopFt - MIN_THICK_FT, d.startBottomFt + deltaFt)
      onResizeBottom(newBotFt)
    }
  }, [onMoveAltitude, onResizeTop, onResizeBottom, height])
  /* eslint-enable react-hooks/exhaustive-deps */

  const finish = useCallback((e) => {
    dragRef.current = null
    if (e && e.currentTarget && e.currentTarget.hasPointerCapture?.(e.pointerId)) {
      e.currentTarget.releasePointerCapture(e.pointerId)
    }
  }, [])

  const topY    = ftToY(topFt, height)
  const bottomY = ftToY(bottomFt, height)
  const minPx   = kind === 'wind' ? MIN_WIND_PX : MIN_BAND_PX
  const bandH   = Math.max(minPx, bottomY - topY)

  // Cursor hint: ns-resize near edges, grab for body. We don't track
  // drag state locally anymore — the cursor reverts on move-out naturally.
  const cursor = kind === 'wind' ? 'grab' : 'grab'

  return (
    <div
      ref={ref}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={finish}
      onPointerCancel={finish}
      style={{
        position: 'absolute',
        top: topY,
        left: 0,
        width,
        height: bandH,
        background:  selected ? `${color}2e` : `${color}14`,
        border: `1px solid ${selected ? color : `${color}66`}`,
        borderRadius: 3,
        cursor,
        padding: kind === 'cloud' ? '4px 8px' : '0 8px',
        display: 'flex',
        alignItems: kind === 'wind' ? 'center' : 'flex-start',
        fontFamily: 'monospace',
        fontSize: 11,
        color: '#e2e8f0',
        userSelect: 'none',
        touchAction: 'none',
        transition: 'background 80ms, border-color 80ms',
        boxSizing: 'border-box',
      }}
    >
      <span style={{ pointerEvents: 'none', overflow: 'hidden', whiteSpace: 'nowrap', textOverflow: 'ellipsis' }}>
        {label}
      </span>
    </div>
  )
}
