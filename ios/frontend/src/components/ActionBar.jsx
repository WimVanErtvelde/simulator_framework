import { useState, useRef } from 'react'
import { useSimStore, CMD } from '../store/useSimStore'

const btnBase = {
  height: 48, minWidth: 130, borderRadius: 4,
  fontFamily: 'monospace', fontSize: 14, fontWeight: 700, letterSpacing: 1,
  cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6,
  transition: 'opacity 0.15s, transform 0.1s',
  userSelect: 'none', WebkitUserSelect: 'none', touchAction: 'manipulation',
}

const disabledStyle = { background: '#0d1117', color: '#30363d', borderColor: '#21262d', cursor: 'not-allowed' }

const confirmStyle = { background: '#4a1060', color: '#bc4fcb', border: '1px solid #7a2090' }

function ActionBtn({ label, style, disabled, onClick, isPending }) {
  const [pressed, setPressed] = useState(false)

  const finalStyle = {
    ...btnBase,
    ...style,
    ...(disabled ? disabledStyle : {}),
    ...(isPending ? confirmStyle : {}),
    ...(pressed && !disabled ? { transform: 'scale(0.97)', opacity: 0.85 } : {}),
  }

  const handleClick = (e) => {
    e.preventDefault()
    if (!disabled) onClick?.()
  }

  return (
    <button
      style={finalStyle}
      disabled={disabled}
      onClick={handleClick}
      onTouchEnd={handleClick}
      onMouseDown={() => setPressed(true)}
      onMouseUp={() => setPressed(false)}
      onTouchStart={() => setPressed(true)}
      onMouseLeave={() => setPressed(false)}
    >
      {isPending ? 'CONFIRM? (tap again)' : label}
    </button>
  )
}

export default function ActionBar() {
  const { simState, wsConnected, wsReconnectCount, sendCommand, requestAction, pendingAction } = useSimStore()
  const [resetOpen, setResetOpen] = useState(false)
  const [simOpen, setSimOpen] = useState(false)
  const resetRef = useRef(null)
  const simRef = useRef(null)

  const freezeDisabled = !wsConnected || simState !== 'RUNNING'
  const runDisabled = !wsConnected || (simState === 'RUNNING' || (simState !== 'READY' && simState !== 'FROZEN'))

  return (
    <div style={{
      background: '#0d1117', borderTop: '1px solid #1e293b',
      height: 60, display: 'flex', alignItems: 'center', gap: 10, padding: '0 14px',
      position: 'relative',
    }}>
      <ActionBtn
        label="FREEZE"
        disabled={freezeDisabled}
        style={{ minWidth: 140, background: '#1a2744', color: '#388bfd', border: '1px solid #2d4a8f' }}
        onClick={() => sendCommand(CMD.FREEZE)}
      />
      <ActionBtn
        label="RUN"
        disabled={runDisabled}
        style={{ minWidth: 140, background: '#1a4731', color: '#3fb950', border: '1px solid #2d6a4f' }}
        onClick={() => sendCommand(CMD.RUN)}
      />

      {/* RESET dropdown */}
      <div style={{ position: 'relative' }} ref={resetRef}>
        <ActionBtn
          label="RESET ▴"
          style={{ background: '#3d2b0a', color: '#d29922', border: '1px solid #7d5a14' }}
          disabled={!wsConnected}
          onClick={() => { setResetOpen(!resetOpen); setSimOpen(false) }}
        />
        {resetOpen && (
          <Dropdown onClose={() => setResetOpen(false)}>
            <DropdownItem
              label="Reset Flight"
              isPending={pendingAction?.type === 'reset_flight'}
              onClick={() => requestAction('reset_flight', () => { sendCommand(CMD.RESET_FLIGHT); setResetOpen(false) })}
            />
            <DropdownItem
              label="Reset Aircraft"
              isPending={pendingAction?.type === 'reset_aircraft'}
              onClick={() => requestAction('reset_aircraft', () => { sendCommand(CMD.RESET_AIRCRAFT); setResetOpen(false) })}
            />
            <DropdownItem
              label="Reset Failures"
              onClick={() => { sendCommand(CMD.RESET_FAILURES); setResetOpen(false) }}
            />
          </Dropdown>
        )}
      </div>

      {/* SIM dropdown */}
      <div style={{ position: 'relative' }} ref={simRef}>
        <ActionBtn
          label="SIM ▴"
          style={{ background: '#1c1c1c', color: '#8b949e', border: '1px solid #30363d' }}
          disabled={!wsConnected}
          onClick={() => { setSimOpen(!simOpen); setResetOpen(false) }}
        />
        {simOpen && (
          <Dropdown onClose={() => setSimOpen(false)}>
            <DropdownItem
              label="Shutdown"
              isPending={pendingAction?.type === 'shutdown'}
              onClick={() => requestAction('shutdown', () => { sendCommand(CMD.SHUTDOWN); setSimOpen(false) })}
            />
          </Dropdown>
        )}
      </div>

      {/* Connection indicator — far right */}
      <div style={{ marginLeft: 'auto', display: 'flex', alignItems: 'center', gap: 8, fontSize: 13, color: '#64748b' }}>
        <span style={{
          width: 10, height: 10, borderRadius: '50%',
          background: wsConnected ? '#3fb950' : '#f85149',
        }} />
        {wsConnected ? 'LIVE' : `DISC (${wsReconnectCount})`}
      </div>
    </div>
  )
}

function Dropdown({ children, onClose }) {
  return (
    <>
      <div
        style={{ position: 'fixed', inset: 0, zIndex: 998 }}
        onClick={onClose} onTouchEnd={(e) => { e.preventDefault(); onClose() }}
      />
      <div style={{
        position: 'absolute', bottom: '100%', left: 0, marginBottom: 4,
        background: '#111827', border: '1px solid #1e293b', borderRadius: 4,
        overflow: 'hidden', zIndex: 999, minWidth: 200,
      }}>
        {children}
      </div>
    </>
  )
}

function DropdownItem({ label, onClick, isPending }) {
  return (
    <button
      onClick={(e) => { e.preventDefault(); e.stopPropagation(); onClick() }}
      onTouchEnd={(e) => { e.preventDefault(); e.stopPropagation(); onClick() }}
      style={{
        display: 'block', width: '100%', padding: '12px 16px', fontSize: 14,
        fontFamily: 'monospace', fontWeight: 600, textAlign: 'left',
        background: isPending ? '#4a1060' : 'transparent',
        color: isPending ? '#bc4fcb' : '#e2e8f0',
        border: 'none', cursor: 'pointer', touchAction: 'manipulation',
      }}
      onMouseEnter={(e) => { if (!isPending) e.currentTarget.style.background = '#21262d' }}
      onMouseLeave={(e) => { if (!isPending) e.currentTarget.style.background = 'transparent' }}
    >
      {isPending ? 'CONFIRM? (tap again)' : label}
    </button>
  )
}
