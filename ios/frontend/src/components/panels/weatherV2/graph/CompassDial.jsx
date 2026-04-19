import { useCallback, useRef, useState } from 'react'

// Draggable aviation compass:
//   0° = North at top, clockwise positive (so 90° = East to the right).
//   Indicator line points from center outward along the value direction
//   — for wind this is the FROM direction (where the wind is coming
//   from).
//
// Props:
//   valueDeg        : current direction 0-359 (true, caller controls frame)
//   onChange(deg)   : fires during drag with integer degree
//   size            : px diameter (default 120)
//   disabled        : bool
//   indicatorColor  : hex (default teal)
export default function CompassDial({
  valueDeg,
  onChange,
  size = 120,
  disabled = false,
  indicatorColor = '#39d0d8',
}) {
  const svgRef = useRef(null)
  const [dragging, setDragging] = useState(false)

  const cx = size / 2
  const cy = size / 2
  const radius = (size / 2) - 8

  const pointerToAngle = useCallback((e) => {
    if (!svgRef.current) return valueDeg
    const rect = svgRef.current.getBoundingClientRect()
    const px = e.clientX - rect.left - cx
    const py = e.clientY - rect.top - cy
    // atan2(px, -py): aviation convention — 0 up (north), clockwise positive.
    let deg = Math.atan2(px, -py) * 180 / Math.PI
    if (deg < 0) deg += 360
    return Math.round(deg) % 360
  }, [cx, cy, valueDeg])

  const emit = useCallback((e) => {
    if (!onChange || disabled) return
    onChange(pointerToAngle(e))
  }, [onChange, disabled, pointerToAngle])

  const onPointerDown = useCallback((e) => {
    if (disabled) return
    e.currentTarget.setPointerCapture(e.pointerId)
    setDragging(true)
    emit(e)
  }, [disabled, emit])

  const onPointerMove = useCallback((e) => {
    if (!dragging) return
    emit(e)
  }, [dragging, emit])

  const onPointerUp = useCallback((e) => {
    if (e.currentTarget.hasPointerCapture?.(e.pointerId)) {
      e.currentTarget.releasePointerCapture(e.pointerId)
    }
    setDragging(false)
  }, [])

  // Tick marks every 30°, cardinals heavier.
  const ticks = []
  for (let a = 0; a < 360; a += 30) {
    const rad = (a - 90) * Math.PI / 180
    const cardinal = a % 90 === 0
    const len = cardinal ? 8 : 5
    ticks.push(
      <line
        key={a}
        x1={cx + Math.cos(rad) * (radius - len)}
        y1={cy + Math.sin(rad) * (radius - len)}
        x2={cx + Math.cos(rad) * radius}
        y2={cy + Math.sin(rad) * radius}
        stroke={cardinal ? '#94a3b8' : '#475569'}
        strokeWidth={cardinal ? 1.5 : 1}
      />
    )
  }

  // Cardinal labels — N bright for unambiguous orientation, E/S/W muted.
  const labels = [
    { text: 'N', a: 0,   color: '#e2e8f0' },
    { text: 'E', a: 90,  color: '#64748b' },
    { text: 'S', a: 180, color: '#64748b' },
    { text: 'W', a: 270, color: '#64748b' },
  ]

  // Indicator from center out along valueDeg.
  const rad = (valueDeg - 90) * Math.PI / 180
  const ix = cx + Math.cos(rad) * (radius - 4)
  const iy = cy + Math.sin(rad) * (radius - 4)

  return (
    <svg
      ref={svgRef}
      width={size} height={size}
      style={{
        touchAction: 'none',
        cursor: disabled ? 'not-allowed' : 'pointer',
        userSelect: 'none',
        opacity: disabled ? 0.4 : 1,
      }}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
    >
      <circle cx={cx} cy={cy} r={radius} fill="#0d1117" stroke="#1e293b" strokeWidth={1} />
      {ticks}
      {labels.map(l => {
        const r = (l.a - 90) * Math.PI / 180
        return (
          <text
            key={l.text}
            x={cx + Math.cos(r) * (radius - 16)}
            y={cy + Math.sin(r) * (radius - 16)}
            fill={l.color}
            fontSize={11}
            fontFamily="monospace"
            fontWeight={700}
            textAnchor="middle"
            dominantBaseline="central"
          >{l.text}</text>
        )
      })}
      <circle cx={cx} cy={cy} r={2} fill="#475569" />
      <line
        x1={cx} y1={cy}
        x2={ix} y2={iy}
        stroke={indicatorColor}
        strokeWidth={2.5}
        strokeLinecap="round"
      />
      <circle cx={ix} cy={iy} r={4} fill={indicatorColor} />
    </svg>
  )
}
