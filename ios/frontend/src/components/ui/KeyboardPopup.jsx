import { useState, useEffect, useRef, useCallback } from 'react'

const PALETTE = {
  bg: '#1c2333',
  btnBg: '#111827',
  border: '#1e293b',
  text: '#e2e8f0',
  dim: '#64748b',
  accent: '#00ff88',
}

const popupStyle = {
  position: 'fixed',
  zIndex: 1000,
  background: PALETTE.bg,
  border: `1px solid ${PALETTE.border}`,
  borderRadius: 6,
  padding: 12,
  maxWidth: 480,
  width: 'calc(100vw - 32px)',
  boxShadow: '0 8px 32px rgba(0,0,0,0.6)',
  outline: 'none',
}

const displayStyle = {
  width: '100%',
  height: 44,
  background: '#0a0e17',
  border: `1px solid ${PALETTE.border}`,
  borderRadius: 3,
  display: 'flex',
  alignItems: 'center',
  padding: '0 12px',
  fontSize: 16,
  fontFamily: 'monospace',
  fontWeight: 700,
  color: PALETTE.accent,
  marginBottom: 8,
  boxSizing: 'border-box',
}

const keyBase = {
  height: 40,
  minWidth: 36,
  borderRadius: 3,
  border: `1px solid ${PALETTE.border}`,
  background: PALETTE.btnBg,
  color: PALETTE.text,
  fontSize: 14,
  fontFamily: 'monospace',
  fontWeight: 700,
  cursor: 'pointer',
  touchAction: 'manipulation',
  userSelect: 'none',
  WebkitUserSelect: 'none',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  flex: '1 1 0',
}

const ROWS = [
  ['Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'],
  ['A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L'],
  ['Z', 'X', 'C', 'V', 'B', 'N', 'M'],
]

/**
 * KeyboardPopup — reusable touch-friendly text input.
 *
 * Props:
 *   value      (string)  initial display value
 *   onSubmit   (fn)      called with display string on ENTER
 *   onCancel   (fn)      called on ESC / outside click
 *   onChange   (fn)      called with display string on every keystroke (for live search)
 *   label      (string)  field name shown at top
 *   anchor     ({x,y})   optional position hint
 *   children   (node)    rendered between display and keyboard (e.g. search results)
 */
export default function KeyboardPopup({
  value = '',
  onSubmit,
  onCancel,
  onChange,
  label = '',
  anchor,
  children,
}) {
  const [display, setDisplay] = useState(value)
  const [shifted, setShifted] = useState(false)
  const popupRef = useRef(null)
  const isFirstRender = useRef(true)

  useEffect(() => {
    popupRef.current?.focus()
  }, [])

  // Notify caller on every display change (skip initial render)
  useEffect(() => {
    if (isFirstRender.current) {
      isFirstRender.current = false
      return
    }
    onChange?.(display)
  }, [display]) // intentionally omit onChange from deps — stable callback from caller

  const appendChar = useCallback((ch) => {
    const c = shifted ? ch.toUpperCase() : ch.toLowerCase()
    setDisplay(prev => prev + c)
    setShifted(false)
  }, [shifted])

  const backspace = useCallback(() => {
    setDisplay(prev => prev.slice(0, -1))
  }, [])

  const handleSubmit = useCallback(() => {
    onSubmit?.(display)
  }, [display, onSubmit])

  const handleKeyDown = useCallback((e) => {
    if (e.key === 'Escape') {
      e.preventDefault()
      onCancel?.()
    } else if (e.key === 'Enter') {
      e.preventDefault()
      handleSubmit()
    } else if (e.key === 'Backspace') {
      e.preventDefault()
      backspace()
    } else if (e.key.length === 1) {
      e.preventDefault()
      setDisplay(prev => prev + e.key)
      setShifted(false)
    }
  }, [handleSubmit, backspace, onCancel])

  const posStyle = {}
  if (anchor) {
    posStyle.left = Math.max(16, Math.min(anchor.x, window.innerWidth - 500))
    posStyle.top = Math.min(anchor.y + 4, window.innerHeight - 320)
  } else {
    posStyle.left = '50%'
    posStyle.top = '50%'
    posStyle.transform = 'translate(-50%, -50%)'
  }

  const renderKey = (ch) => (shifted ? ch.toUpperCase() : ch.toLowerCase())

  return (
    <>
      <div
        style={{ position: 'fixed', inset: 0, zIndex: 999, background: 'rgba(0,0,0,0.3)' }}
        onClick={onCancel}
        onTouchEnd={(e) => { e.preventDefault(); onCancel?.() }}
      />
      <div
        ref={popupRef}
        tabIndex={-1}
        onKeyDown={handleKeyDown}
        style={{ ...popupStyle, ...posStyle }}
      >
        {/* Header */}
        {label && (
          <div style={{
            marginBottom: 6, fontSize: 11, fontFamily: 'monospace',
            color: PALETTE.text, fontWeight: 700, letterSpacing: 1,
          }}>{label}</div>
        )}

        {/* Display */}
        <div style={displayStyle}>
          {display}<span style={{ opacity: 0.4 }}>|</span>
        </div>

        {/* Caller-provided content (e.g. autocomplete results) */}
        {children}

        {/* Rows */}
        {ROWS.map((row, ri) => (
          <div key={ri} style={{
            display: 'flex', gap: 3, marginBottom: 3,
            justifyContent: 'center',
          }}>
            {ri === 2 && (
              <button
                style={{
                  ...keyBase, minWidth: 52,
                  background: shifted ? PALETTE.accent : PALETTE.btnBg,
                  color: shifted ? '#0a0e17' : PALETTE.dim,
                  border: `1px solid ${shifted ? PALETTE.accent : PALETTE.border}`,
                }}
                onClick={() => setShifted(s => !s)}
              >SHIFT</button>
            )}
            {row.map(ch => (
              <button key={ch} style={keyBase} onClick={() => appendChar(ch)}>
                {renderKey(ch)}
              </button>
            ))}
            {ri === 2 && (
              <button style={{ ...keyBase, minWidth: 52, fontSize: 18 }} onClick={backspace}>
                &#9003;
              </button>
            )}
          </div>
        ))}

        {/* Bottom row: space + number row + actions */}
        <div style={{ display: 'flex', gap: 3, marginTop: 1 }}>
          <button
            style={{
              ...keyBase, height: 40,
              background: 'transparent', color: PALETTE.dim,
              border: `1px solid ${PALETTE.border}`,
              minWidth: 52,
            }}
            onClick={onCancel}
          >ESC</button>
          <button
            style={{ ...keyBase, flex: '3 1 0' }}
            onClick={() => { setDisplay(prev => prev + ' '); setShifted(false) }}
          >SPACE</button>
          {['0','1','2','3','4','5','6','7','8','9'].map(d => (
            <button key={d} style={{ ...keyBase, minWidth: 28 }}
              onClick={() => setDisplay(prev => prev + d)}
            >{d}</button>
          ))}
          <button
            style={{
              ...keyBase, height: 40, minWidth: 64,
              background: PALETTE.accent, color: '#0a0e17',
              border: `1px solid ${PALETTE.accent}`,
            }}
            onClick={handleSubmit}
          >ENTER</button>
        </div>
      </div>
    </>
  )
}
