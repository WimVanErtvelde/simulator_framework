import { useCallback, useEffect, useRef, useState } from 'react'
import { MAX_FT, MIN_FT } from './mslScale'

// Horizontal slider whose handle X-position uses the same sqrt mapping
// as the MSL graph axis. Drag a tall band in the graph and the handle
// moves in lockstep with the band visually (both use sqrt(ft/MAX)).
//
// Props:
//   valueFt:   current altitude (ft MSL)
//   minFt/maxFt: tighter bounds (e.g., top ≥ base+100)
//   onChange(ft): fires during drag with rounded ft
//   disabled:  boolean
export default function NonLinearSlider({
  valueFt,
  minFt = MIN_FT,
  maxFt = MAX_FT,
  onChange,
  disabled = false,
}) {
  const trackRef = useRef(null)
  const [dragging, setDragging] = useState(false)
  const [widthPx, setWidthPx] = useState(200)   // updated by ResizeObserver

  useEffect(() => {
    if (!trackRef.current) return
    // ResizeObserver fires an initial callback on observe(), so the
    // handle lands correctly on the next frame after mount.
    const ro = new ResizeObserver(entries => {
      for (const entry of entries) setWidthPx(entry.contentRect.width)
    })
    ro.observe(trackRef.current)
    return () => ro.disconnect()
  }, [])

  const ftToX = (ft, w) => {
    const clamped = Math.max(MIN_FT, Math.min(MAX_FT, ft))
    return w * Math.sqrt(clamped / MAX_FT)
  }
  const xToFt = (x, w) => {
    const frac = Math.max(0, Math.min(1, x / Math.max(1, w)))
    return frac * frac * MAX_FT
  }

  const emit = useCallback((clientX) => {
    if (!trackRef.current) return
    const rect = trackRef.current.getBoundingClientRect()
    const ft = xToFt(clientX - rect.left, rect.width)
    const clamped = Math.max(minFt, Math.min(maxFt, ft))
    if (onChange) onChange(Math.round(clamped))
  }, [minFt, maxFt, onChange])

  const onPointerDown = useCallback((e) => {
    if (disabled) return
    e.currentTarget.setPointerCapture(e.pointerId)
    setDragging(true)
    emit(e.clientX)
  }, [disabled, emit])

  const onPointerMove = useCallback((e) => {
    if (!dragging) return
    emit(e.clientX)
  }, [dragging, emit])

  const onPointerUp = useCallback((e) => {
    if (e.currentTarget.hasPointerCapture?.(e.pointerId)) {
      e.currentTarget.releasePointerCapture(e.pointerId)
    }
    setDragging(false)
  }, [])

  const handleX = ftToX(valueFt, widthPx)
  const TICKS = [0, 5000, 10000, 20000, 30000, 50000]

  return (
    <div
      ref={trackRef}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
      style={{
        position: 'relative',
        width: '100%',
        height: 28,
        cursor: disabled ? 'not-allowed' : 'pointer',
        touchAction: 'none',
        opacity: disabled ? 0.4 : 1,
        userSelect: 'none',
      }}
    >
      {/* Track */}
      <div style={{
        position: 'absolute',
        top: '50%', left: 0, right: 0,
        height: 4,
        background: '#1c2333',
        border: '1px solid #1e293b',
        borderRadius: 2,
        transform: 'translateY(-50%)',
        pointerEvents: 'none',
      }} />
      {/* Tick marks at canonical altitudes */}
      {TICKS.map(alt => (
        <div key={alt} style={{
          position: 'absolute',
          top: '50%', left: ftToX(alt, widthPx),
          width: 1, height: 8,
          background: '#334155',
          transform: 'translate(-50%, -50%)',
          pointerEvents: 'none',
        }} />
      ))}
      {/* Handle */}
      <div style={{
        position: 'absolute',
        top: '50%', left: handleX,
        width: 12, height: 20,
        background: dragging ? '#39d0d8' : '#64748b',
        border: '1px solid #39d0d8',
        borderRadius: 2,
        transform: 'translate(-50%, -50%)',
        transition: dragging ? 'none' : 'background 80ms',
        pointerEvents: 'none',
      }} />
    </div>
  )
}
