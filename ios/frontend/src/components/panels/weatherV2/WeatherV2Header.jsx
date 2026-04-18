import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import { useShallow } from 'zustand/react/shallow'

// Toolbar row: Metric/Imperial toggle on left, dirty indicator + Accept/Discard
// on right. Sits below the SidePanel's own title header.
//
// Slice 5a-i: accept() is a logging no-op with a fake commit to serverState so
// dirty-state behavior can be tested end-to-end. Slice 5a-ii wires the real
// WebSocket send.
export default function WeatherV2Header() {
  const { draft, serverState, units, setUnits, accept, discard } = useWeatherV2Store(
    useShallow(s => ({
      draft:       s.draft,
      serverState: s.serverState,
      units:       s.units,
      setUnits:    s.setUnits,
      accept:      s.accept,
      discard:     s.discard,
    }))
  )
  // Reactive dirty check — draft/serverState are in the selector, so Zustand
  // re-renders this component whenever either changes.
  const isDirty = JSON.stringify(draft) !== JSON.stringify(serverState)

  return (
    <div style={{
      display: 'flex', alignItems: 'center', justifyContent: 'space-between',
      padding: '0 0 10px', borderBottom: '1px solid #1e293b', marginBottom: 10,
      gap: 8,
    }}>
      <div style={{ display: 'flex', gap: 2 }}>
        <UnitBtn active={units === 'metric'}   onClick={() => setUnits('metric')}>METRIC</UnitBtn>
        <UnitBtn active={units === 'imperial'} onClick={() => setUnits('imperial')}>IMPERIAL</UnitBtn>
      </div>

      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        {isDirty && (
          <span style={{
            fontSize: 10, color: '#f59e0b', fontFamily: 'monospace',
            letterSpacing: 1, textTransform: 'uppercase',
          }}>unsaved</span>
        )}
        <ActionBtn primary={false} enabled={isDirty} onClick={discard}>DISCARD</ActionBtn>
        <ActionBtn primary={true}  enabled={isDirty} onClick={accept}>ACCEPT</ActionBtn>
      </div>
    </div>
  )
}

function UnitBtn({ active, onClick, children }) {
  return (
    <button
      type="button"
      onClick={onClick}
      onTouchEnd={(e) => { e.preventDefault(); onClick?.() }}
      style={{
        padding: '6px 10px', fontSize: 10, fontWeight: 700, letterSpacing: 1,
        textTransform: 'uppercase', fontFamily: 'monospace',
        border: '1px solid', borderRadius: 3, cursor: 'pointer',
        touchAction: 'manipulation',
        borderColor: active ? '#39d0d8' : '#1e293b',
        background:  active ? 'rgba(57, 208, 216, 0.10)' : '#111827',
        color:       active ? '#39d0d8' : '#64748b',
      }}
    >{children}</button>
  )
}

function ActionBtn({ primary, enabled, onClick, children }) {
  return (
    <button
      type="button"
      disabled={!enabled}
      onClick={onClick}
      onTouchEnd={(e) => { e.preventDefault(); if (enabled) onClick?.() }}
      style={{
        padding: '6px 12px', fontSize: 10, fontWeight: 700, letterSpacing: 1,
        textTransform: 'uppercase', fontFamily: 'monospace',
        border: '1px solid', borderRadius: 3,
        cursor: enabled ? 'pointer' : 'not-allowed',
        opacity: enabled ? 1 : 0.4,
        touchAction: 'manipulation',
        borderColor: primary ? '#bc4fcb' : '#1e293b',
        background:  primary ? 'rgba(188, 79, 203, 0.12)' : '#1c2333',
        color:       primary ? '#bc4fcb' : '#94a3b8',
      }}
    >{children}</button>
  )
}
