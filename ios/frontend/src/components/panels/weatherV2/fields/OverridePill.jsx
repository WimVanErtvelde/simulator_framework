// Override/inherit toggle pill shown in a field header when
// showOverrideToggle is true (patch tab context). On Global the pill
// isn't rendered; fields are always authoring the real value.
//
// Magenta for "overriding" (authoring a value distinct from Global),
// grey for "inheriting" (will use Global's value at commit time).
export default function OverridePill({ enabled, onToggle }) {
  return (
    <button
      type="button"
      onClick={() => onToggle?.(!enabled)}
      onTouchEnd={(e) => { e.preventDefault(); onToggle?.(!enabled) }}
      style={{
        height: 18, padding: '0 8px',
        background: enabled ? 'rgba(188, 79, 203, 0.12)' : '#111827',
        border: `1px solid ${enabled ? '#bc4fcb' : '#1e293b'}`,
        borderRadius: 3,
        color: enabled ? '#bc4fcb' : '#475569',
        fontSize: 9, fontFamily: 'monospace',
        fontWeight: 700, letterSpacing: 1,
        cursor: 'pointer',
        touchAction: 'manipulation',
      }}
    >
      {enabled ? 'OVERRIDE' : 'INHERIT'}
    </button>
  )
}
