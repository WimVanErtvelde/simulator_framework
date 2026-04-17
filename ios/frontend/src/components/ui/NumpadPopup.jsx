import { useState, useEffect, useRef, useCallback } from 'react'

const PALETTE = {
  bg: '#1c2333',
  btnBg: '#111827',
  border: '#1e293b',
  text: '#e2e8f0',
  dim: '#64748b',
  accent: '#00ff88',
  danger: '#ff3b30',
}

const popupStyle = {
  position: 'fixed',
  zIndex: 1000,
  background: PALETTE.bg,
  border: `1px solid ${PALETTE.border}`,
  borderRadius: 6,
  padding: 12,
  width: 240,
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
  justifyContent: 'flex-end',
  padding: '0 12px',
  fontSize: 20,
  fontFamily: 'monospace',
  fontWeight: 700,
  color: PALETTE.accent,
  marginBottom: 8,
  transition: 'border-color 0.15s',
  boxSizing: 'border-box',
}

const btnBase = {
  height: 48,
  borderRadius: 3,
  border: `1px solid ${PALETTE.border}`,
  background: PALETTE.btnBg,
  color: PALETTE.text,
  fontSize: 16,
  fontFamily: 'monospace',
  fontWeight: 700,
  cursor: 'pointer',
  touchAction: 'manipulation',
  userSelect: 'none',
  WebkitUserSelect: 'none',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
}

const DIGITS = ['7', '8', '9', '4', '5', '6', '1', '2', '3']

/**
 * NumpadPopup — reusable touch-friendly numeric input.
 *
 * Props:
 *   value          (string)  initial display value
 *   onSubmit       (fn)      called with display string on SET
 *   onCancel       (fn)      called on ESC / outside click
 *   label          (string)  field name shown at top left
 *   hint           (string)  optional range hint shown at top right
 *   allowDecimal   (bool)    show decimal button (default true)
 *   allowedDigits  (string)  which digits are enabled (default "0123456789")
 *   anchor         ({x,y})   optional position hint
 *   error          (bool)    flash display red when true
 */
export default function NumpadPopup({
  value = '',
  onSubmit,
  onCancel,
  label = '',
  hint = '',
  allowDecimal = true,
  allowedDigits = '0123456789',
  autoDecimalAfter = 0,  // auto-insert '.' after N digits (0 = off)
  anchor,
  error = false,
}) {
  const [display, setDisplay] = useState(value)
  const [pristine, setPristine] = useState(true) // first input clears initial value
  const popupRef = useRef(null)
  const errorTimerRef = useRef(null)
  const [flashError, setFlashError] = useState(false)
  const mountedRef = useRef(false)

  // Allow digit input starting next frame — prevents the opening
  // click from propagating into the "7" button
  useEffect(() => {
    requestAnimationFrame(() => { mountedRef.current = true })
    return () => { mountedRef.current = false }
  }, [])

  // Flash error when error prop goes true
  useEffect(() => {
    if (error) {
      setFlashError(true)
      clearTimeout(errorTimerRef.current)
      errorTimerRef.current = setTimeout(() => setFlashError(false), 400)
    }
    return () => clearTimeout(errorTimerRef.current)
  }, [error])

  // Auto-focus on mount
  useEffect(() => {
    popupRef.current?.focus()
  }, [])

  const appendDigit = useCallback((d) => {
    if (!mountedRef.current) return
    if (!allowedDigits.includes(d)) return
    if (pristine) {
      setDisplay(d)
      setPristine(false)
    } else {
      setDisplay(prev => {
        // Auto-insert decimal after N digits (e.g. "118" → "118.")
        if (autoDecimalAfter > 0 && !prev.includes('.')) {
          const digitCount = prev.replace(/[^0-9]/g, '').length
          if (digitCount === autoDecimalAfter) {
            return prev + '.' + d
          }
        }
        return prev + d
      })
    }
  }, [allowedDigits, pristine, autoDecimalAfter])

  const appendDecimal = useCallback(() => {
    if (!mountedRef.current) return
    if (!allowDecimal) return
    if (pristine) {
      setDisplay('.')
      setPristine(false)
    } else {
      setDisplay(prev => prev.includes('.') ? prev : prev + '.')
    }
  }, [allowDecimal, pristine])

  const backspace = useCallback(() => {
    setPristine(false)
    setDisplay(prev => prev.slice(0, -1))
  }, [])

  const handleSubmit = useCallback(() => {
    onSubmit?.(display)
  }, [display, onSubmit])

  const handleKeyDown = useCallback((e) => {
    if (e.key >= '0' && e.key <= '9') {
      e.preventDefault()
      appendDigit(e.key)
    } else if (e.key === '.' || e.key === ',') {
      e.preventDefault()
      appendDecimal()
    } else if (e.key === 'Backspace') {
      e.preventDefault()
      backspace()
    } else if (e.key === 'Enter') {
      e.preventDefault()
      handleSubmit()
    } else if (e.key === 'Escape') {
      e.preventDefault()
      onCancel?.()
    }
  }, [appendDigit, appendDecimal, backspace, handleSubmit, onCancel])

  // Position popup near anchor or center
  const posStyle = {}
  if (anchor) {
    // Try to place below-right of anchor, but keep on screen
    posStyle.left = Math.min(anchor.x, window.innerWidth - 260)
    posStyle.top = Math.min(anchor.y + 4, window.innerHeight - 420)
  } else {
    posStyle.left = '50%'
    posStyle.top = '50%'
    posStyle.transform = 'translate(-50%, -50%)'
  }

  const digitEnabled = (d) => allowedDigits.includes(d)

  return (
    <>
      {/* Backdrop */}
      <div
        style={{ position: 'fixed', inset: 0, zIndex: 999, background: 'rgba(0,0,0,0.3)' }}
        onClick={onCancel}
        onTouchEnd={(e) => { e.preventDefault(); onCancel?.() }}
      />
      {/* Popup */}
      <div
        ref={popupRef}
        tabIndex={-1}
        onKeyDown={handleKeyDown}
        style={{ ...popupStyle, ...posStyle }}
      >
        {/* Header */}
        <div style={{
          display: 'flex', justifyContent: 'space-between', alignItems: 'center',
          marginBottom: 6, fontSize: 11, fontFamily: 'monospace',
        }}>
          <span style={{ color: PALETTE.text, fontWeight: 700, letterSpacing: 1 }}>{label}</span>
          {hint && <span style={{ color: PALETTE.dim }}>{hint}</span>}
        </div>

        {/* Display */}
        <div style={{
          ...displayStyle,
          borderColor: flashError ? PALETTE.danger : PALETTE.border,
          color: flashError ? PALETTE.danger : PALETTE.accent,
        }}>
          {display || <span style={{ color: PALETTE.dim }}>0</span>}
        </div>

        {/* Grid: 3x3 digits + bottom row */}
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 4 }}>
          {DIGITS.map(d => (
            <button key={d} style={{
              ...btnBase,
              opacity: digitEnabled(d) ? 1 : 0.25,
              cursor: digitEnabled(d) ? 'pointer' : 'not-allowed',
            }}
              onClick={() => appendDigit(d)}
              disabled={!digitEnabled(d)}
            >{d}</button>
          ))}

          {/* Bottom row: decimal / 0 / backspace */}
          {allowDecimal ? (
            <button style={btnBase} onClick={appendDecimal}>.</button>
          ) : (
            <div />
          )}
          <button style={{
            ...btnBase,
            opacity: digitEnabled('0') ? 1 : 0.25,
            cursor: digitEnabled('0') ? 'pointer' : 'not-allowed',
          }}
            onClick={() => appendDigit('0')}
            disabled={!digitEnabled('0')}
          >0</button>
          <button style={{ ...btnBase, fontSize: 20 }} onClick={backspace}>&#9003;</button>
        </div>

        {/* Action row */}
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 4, marginTop: 4 }}>
          <button
            style={{
              ...btnBase, height: 44,
              background: 'transparent', color: PALETTE.dim,
              border: `1px solid ${PALETTE.border}`,
            }}
            onClick={onCancel}
          >ESC</button>
          <button
            style={{
              ...btnBase, height: 44,
              background: PALETTE.accent, color: '#0a0e17',
              border: `1px solid ${PALETTE.accent}`,
            }}
            onClick={handleSubmit}
          >SET</button>
        </div>
      </div>
    </>
  )
}
