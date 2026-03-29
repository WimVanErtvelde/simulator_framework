import { useSimStore, CMD } from '../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { SectionHeader, FullWidthBtn } from './PanelUtils'

const STATUS_COLORS = {
  OK:       { bg: '#00ff88', shadow: 'rgba(0,255,136,0.5)' },
  DEGRADED: { bg: '#d29922', shadow: 'rgba(210,153,34,0.5)' },
  LOST:     { bg: '#ff3b30', shadow: 'rgba(255,59,48,0.5)' },
  OFFLINE:  { bg: '#334155', shadow: 'none' },
}

const LIFECYCLE_BADGES = {
  active:       { color: '#00ff88', border: '#00ff88' },
  inactive:     { color: '#d29922', border: '#d29922' },
  unknown:      { color: '#64748b', border: '#334155' },
  unconfigured: { color: '#64748b', border: '#334155' },
  finalized:    { color: '#ff3b30', border: '#ff3b30' },
  reloading:    { color: '#39d0d8', border: '#39d0d8' },
  error:        { color: '#ff3b30', border: '#ff3b30' },
}

const btnSmall = {
  height: 28, minWidth: 50, padding: '0 6px',
  background: '#1c2333', border: '1px solid #1e293b', borderRadius: 3,
  color: '#64748b', fontSize: 9, fontFamily: 'monospace', fontWeight: 700,
  letterSpacing: 1, textTransform: 'uppercase',
  cursor: 'pointer', touchAction: 'manipulation', transition: 'opacity 0.15s',
}

const btnAmber = {
  ...btnSmall,
  background: 'rgba(210, 153, 34, 0.08)',
  border: '1px solid #d29922',
  color: '#d29922',
}

function SmallBtn({ label, style, onClick, disabled }) {
  return (
    <button
      onClick={(e) => { e.preventDefault(); onClick() }}
      onTouchEnd={(e) => { e.preventDefault(); onClick() }}
      style={{ ...style, ...(disabled ? { opacity: 0.3, pointerEvents: 'none' } : {}) }}
      disabled={disabled}
    >{label}</button>
  )
}

// Sort order: OK first, then DEGRADED, LOST, OFFLINE last
const STATUS_ORDER = { OK: 0, DEGRADED: 1, LOST: 2, OFFLINE: 3 }

export default function NodesPanel() {
  const { nodes, sendCommand } = useSimStore(useShallow(s => ({
    nodes: s.nodes, sendCommand: s.sendCommand,
  })))
  const entries = Object.entries(nodes)
    .sort(([, a], [, b]) => {
      const aOrder = STATUS_ORDER[a.status] ?? 3
      const bOrder = STATUS_ORDER[b.status] ?? 3
      if (aOrder !== bOrder) return aOrder - bOrder
      // Within same status, known nodes first, then alphabetical
      if (a.known !== b.known) return a.known ? -1 : 1
      return 0
    })
    .sort(([a], [b]) => {
      // Secondary: alphabetical within same group
      const aInfo = nodes[a], bInfo = nodes[b]
      const aOrder = STATUS_ORDER[aInfo.status] ?? 3
      const bOrder = STATUS_ORDER[bInfo.status] ?? 3
      if (aOrder !== bOrder) return aOrder - bOrder
      return a.localeCompare(b)
    })

  if (entries.length === 0) {
    return (
      <div style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>
        <SectionHeader title="NODE STATUS" />
        <div style={{
          flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center',
          color: '#64748b', fontSize: 13, fontFamily: 'monospace',
        }}>
          Waiting for node heartbeats...
        </div>
      </div>
    )
  }

  // Count by status
  const counts = { OK: 0, DEGRADED: 0, LOST: 0, OFFLINE: 0 }
  entries.forEach(([, info]) => { counts[info.status] = (counts[info.status] || 0) + 1 })

  return (
    <div>
      {/* Summary bar */}
      <div style={{
        display: 'flex', gap: 12, marginBottom: 12, padding: '8px 10px',
        background: '#111827', border: '1px solid #1e293b', borderRadius: 3,
        fontFamily: 'monospace', fontSize: 12,
      }}>
        {counts.OK > 0 && <span style={{ color: '#00ff88' }}>{counts.OK} OK</span>}
        {counts.DEGRADED > 0 && <span style={{ color: '#d29922' }}>{counts.DEGRADED} DEG</span>}
        {counts.LOST > 0 && <span style={{ color: '#ff3b30' }}>{counts.LOST} LOST</span>}
        {counts.OFFLINE > 0 && <span style={{ color: '#64748b' }}>{counts.OFFLINE} OFF</span>}
        <span style={{ color: '#475569', marginLeft: 'auto' }}>{entries.length} total</span>
      </div>

      <FullWidthBtn
        label="RELOAD ALL"
        style={{ marginBottom: 12 }}
        onClick={() => entries
          .filter(([, info]) => info.status !== 'OFFLINE')
          .forEach(([name]) => sendCommand(CMD.RELOAD_NODE, { node_name: name }))}
      />

      <SectionHeader title="NODE STATUS" />
      {entries.map(([name, info]) => {
        const statusColors = STATUS_COLORS[info.status] ?? STATUS_COLORS.OFFLINE
        const lcState = info.lifecycle_state ?? 'unknown'
        const lcBadge = LIFECYCLE_BADGES[lcState] ?? LIFECYCLE_BADGES.unknown
        const isOffline = info.status === 'OFFLINE'

        // Contextual primary action
        const isActive = lcState === 'active'
        const primaryCmd = isActive ? CMD.DEACTIVATE_NODE : CMD.ACTIVATE_NODE
        const primaryLabel = isActive ? 'DEACT' : 'ACTIV'

        return (
          <div key={name} style={{
            background: '#111827', border: '1px solid #1e293b', borderRadius: 3,
            padding: '10px 12px', marginBottom: 4,
            opacity: isOffline ? 0.5 : 1,
          }}>
            {/* Top row: status dot, name, lifecycle badge, last seen */}
            <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 6 }}>
              <span style={{
                width: 12, height: 12, borderRadius: '50%', flexShrink: 0,
                background: statusColors.bg,
                boxShadow: statusColors.shadow !== 'none' ? `0 0 6px ${statusColors.shadow}` : 'none',
              }} />
              <span style={{ fontSize: 14, fontFamily: 'monospace', color: '#e2e8f0' }}>{name}</span>
              <span style={{
                fontSize: 9, fontFamily: 'monospace', fontWeight: 700, letterSpacing: 1,
                textTransform: 'uppercase', padding: '2px 6px', borderRadius: 3,
                color: isOffline ? '#475569' : lcBadge.color,
                border: `1px solid ${isOffline ? '#334155' : lcBadge.border}`,
                background: 'transparent', marginLeft: 'auto', flexShrink: 0,
              }}>{isOffline ? 'offline' : lcState}</span>
              <span style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace', flexShrink: 0 }}>
                {isOffline ? '--' : (info.last_seen_sec != null ? `${info.last_seen_sec.toFixed(1)}s` : '--')}
              </span>
            </div>
            {/* Error detail — shown when node has a last_error */}
            {info.last_error && (lcState === 'unconfigured' || lcState === 'error' || info.status === 'LOST') && (
              <div style={{
                marginLeft: 22, marginBottom: 6, padding: '4px 8px',
                background: 'rgba(255, 59, 48, 0.08)', border: '1px solid rgba(255, 59, 48, 0.3)',
                borderRadius: 3, fontSize: 11, fontFamily: 'monospace', color: '#ff6b6b',
                lineHeight: 1.4, wordBreak: 'break-word',
              }}>
                {info.last_error}
              </div>
            )}
            {/* Bottom row: action buttons */}
            <div style={{ display: 'flex', gap: 6, marginLeft: 22 }}>
              <SmallBtn
                label={primaryLabel}
                style={btnSmall}
                onClick={() => sendCommand(primaryCmd, { node_name: name })}
                disabled={isOffline}
              />
              <SmallBtn
                label="RESET"
                style={btnAmber}
                onClick={() => sendCommand(CMD.RESET_NODE, { node_name: name })}
                disabled={isOffline}
              />
              <SmallBtn
                label="RELOAD"
                style={btnSmall}
                onClick={() => sendCommand(CMD.RELOAD_NODE, { node_name: name })}
                disabled={isOffline}
              />
            </div>
          </div>
        )
      })}
    </div>
  )
}
