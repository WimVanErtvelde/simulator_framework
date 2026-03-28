export default function ToggleSwitch({ label, on = false, onToggle, disabled = false }) {
  return (
    <button
      onClick={disabled ? undefined : onToggle}
      style={{
        display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 2,
        background: 'none', border: 'none', cursor: disabled ? 'default' : 'pointer',
        padding: 4, opacity: disabled ? 0.4 : 1,
      }}
    >
      <div style={{
        width: 36, height: 56, borderRadius: 4,
        background: '#111827', border: `1px solid ${on ? '#00ff88' : '#1e293b'}`,
        position: 'relative', transition: 'border-color 0.15s',
      }}>
        {/* Lever */}
        <div style={{
          position: 'absolute', left: 4, right: 4, height: 22, borderRadius: 3,
          top: on ? 4 : 30, background: on ? '#00ff88' : '#374151',
          transition: 'top 0.15s, background 0.15s',
        }} />
      </div>
      <div style={{
        color: on ? '#00ff88' : '#64748b', fontSize: 8, fontWeight: 600,
        fontFamily: "'JetBrains Mono', monospace", textAlign: 'center',
        maxWidth: 48, lineHeight: 1.1,
      }}>
        {label}
      </div>
    </button>
  )
}
