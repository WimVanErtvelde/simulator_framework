import { useRef, useState, useCallback } from 'react'
import { ftToY, ftPerPx } from './mslScale'

const EDGE_HIT_PX   = 8      // drag-resize sensitivity near top/bottom edges
const MIN_BAND_PX   = 12     // minimum rendered band height for clouds
const MIN_WIND_PX   = 3      // point-altitude bar for winds

// Draggable layer band on the MSL graph.
//
// Drag preview is LOCAL (useState). Callbacks fire ONCE on pointer-up with
// the final delta — avoids spamming WS remove+add on every pointer-move.
//
// Props:
//   kind          : 'cloud' | 'wind'  — affects visual + resize behavior
//   topFt         : top altitude MSL in ft (higher altitude)
//   bottomFt      : bottom altitude MSL in ft (lower altitude)
//                   For wind: pass the same value for both (thin bar).
//   height        : pixel height of the graph viewport
//   width         : pixel width of the band column
//   selected      : boolean — highlight state
//   label         : string displayed in the band
//   color         : base color for the band
//   onSelect      : () => void (fires on pointer-down)
//   onCommitMove(deltaFt)           : fires once on pointer-up
//   onCommitResizeTop(newTopFt)     : fires once on pointer-up (cloud only)
//   onCommitResizeBottom(newBotFt)  : fires once on pointer-up (cloud only)
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
  onCommitMove,
  onCommitResizeTop,
  onCommitResizeBottom,
}) {
  const ref = useRef(null)
  const startRef = useRef(null)        // { edge, startY, startTopFt, startBottomFt }
  const [drag, setDrag] = useState(null)  // { edge, deltaFt } | null

  const detectEdge = (clientY) => {
    if (kind === 'wind') return 'body'
    const rect = ref.current.getBoundingClientRect()
    const localY = clientY - rect.top
    if (localY <= EDGE_HIT_PX)               return 'top'
    if (localY >= rect.height - EDGE_HIT_PX) return 'bottom'
    return 'body'
  }

  const onPointerDown = useCallback((e) => {
    e.stopPropagation()
    if (onSelect) onSelect()
    e.currentTarget.setPointerCapture(e.pointerId)
    const edge = detectEdge(e.clientY)
    startRef.current = {
      edge,
      startY: e.clientY,
      startTopFt: topFt,
      startBottomFt: bottomFt,
    }
    setDrag({ edge, deltaFt: 0 })
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [onSelect, topFt, bottomFt, kind])

  const onPointerMove = useCallback((e) => {
    if (!startRef.current) return
    const { startY } = startRef.current
    const deltaPx = e.clientY - startY
    // Pointer DOWN in pixels = LOWER altitude (higher Y, lower ft). Negate.
    const deltaFt = -deltaPx * ftPerPx(height)
    setDrag({ edge: startRef.current.edge, deltaFt })
  }, [height])

  const finish = useCallback((e) => {
    const info = startRef.current
    const d    = drag
    startRef.current = null
    setDrag(null)
    if (e && e.currentTarget && e.currentTarget.hasPointerCapture?.(e.pointerId)) {
      e.currentTarget.releasePointerCapture(e.pointerId)
    }
    if (!info || !d || d.deltaFt === 0) return
    if (info.edge === 'body'   && onCommitMove)          onCommitMove(d.deltaFt)
    if (info.edge === 'top'    && onCommitResizeTop)     onCommitResizeTop(info.startTopFt + d.deltaFt)
    if (info.edge === 'bottom' && onCommitResizeBottom)  onCommitResizeBottom(info.startBottomFt + d.deltaFt)
  }, [drag, onCommitMove, onCommitResizeTop, onCommitResizeBottom])

  // Compute display altitudes — apply drag delta to the edge being manipulated.
  let displayTopFt = topFt
  let displayBottomFt = bottomFt
  if (drag) {
    if (drag.edge === 'body') {
      displayTopFt    = topFt + drag.deltaFt
      displayBottomFt = bottomFt + drag.deltaFt
    } else if (drag.edge === 'top') {
      displayTopFt = Math.max(bottomFt + 100, topFt + drag.deltaFt)
    } else if (drag.edge === 'bottom') {
      displayBottomFt = Math.min(topFt - 100, bottomFt + drag.deltaFt)
    }
  }

  const topY    = ftToY(displayTopFt, height)
  const bottomY = ftToY(displayBottomFt, height)
  const minPx   = kind === 'wind' ? MIN_WIND_PX : MIN_BAND_PX
  const bandH   = Math.max(minPx, bottomY - topY)

  const edgeCursor =
    drag?.edge === 'top' || drag?.edge === 'bottom' ? 'ns-resize' :
    drag?.edge === 'body' ? 'grabbing' : 'grab'

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
        cursor: edgeCursor,
        padding: kind === 'cloud' ? '4px 8px' : '0 8px',
        display: 'flex',
        alignItems: kind === 'wind' ? 'center' : 'flex-start',
        fontFamily: 'monospace',
        fontSize: 11,
        color: '#e2e8f0',
        userSelect: 'none',
        touchAction: 'none',
        transition: drag ? 'none' : 'background 80ms, border-color 80ms',
        boxSizing: 'border-box',
      }}
    >
      <span style={{ pointerEvents: 'none', overflow: 'hidden', whiteSpace: 'nowrap', textOverflow: 'ellipsis' }}>
        {label}
      </span>
    </div>
  )
}
