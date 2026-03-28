import { useRef, useCallback } from 'react'

export default function VerticalSlider({
  value = 0, min = 0, max = 1, label = '', color = '#333',
  height = 160, width = 40, onChange,
}) {
  const trackRef = useRef(null)
  const pct = max > min ? (value - min) / (max - min) : 0

  const handleInteraction = useCallback((clientY) => {
    if (!trackRef.current || !onChange) return
    const rect = trackRef.current.getBoundingClientRect()
    const y = clientY - rect.top
    const norm = 1 - Math.max(0, Math.min(1, y / rect.height))
    onChange(min + norm * (max - min))
  }, [onChange, min, max])

  const onPointerDown = (e) => {
    e.preventDefault()
    handleInteraction(e.clientY)
    const onMove = (ev) => handleInteraction(ev.clientY)
    const onUp = () => { window.removeEventListener('pointermove', onMove); window.removeEventListener('pointerup', onUp) }
    window.addEventListener('pointermove', onMove)
    window.addEventListener('pointerup', onUp)
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 4 }}>
      <div style={{ color: '#94a3b8', fontSize: 9, fontFamily: "'JetBrains Mono', monospace" }}>{label}</div>
      <div
        ref={trackRef}
        onPointerDown={onPointerDown}
        style={{
          width, height, background: '#0a0e14', border: '1px solid #1e293b',
          borderRadius: 4, position: 'relative', cursor: 'pointer', touchAction: 'none',
        }}
      >
        {/* Fill */}
        <div style={{
          position: 'absolute', bottom: 0, left: 2, right: 2,
          height: `${pct * 100}%`, background: 'rgba(100,116,139,0.2)',
          borderRadius: 2, transition: 'height 0.05s',
        }} />
        {/* Knob */}
        <div style={{
          position: 'absolute', left: -2, right: -2, height: 16,
          bottom: `calc(${pct * 100}% - 8px)`,
          background: color, borderRadius: 4, border: '1px solid rgba(255,255,255,0.2)',
          transition: 'bottom 0.05s',
        }} />
      </div>
      <div style={{ color: '#e2e8f0', fontSize: 10, fontFamily: "'JetBrains Mono', monospace" }}>
        {(pct * 100).toFixed(0)}%
      </div>
    </div>
  )
}
