import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import { useSimStore } from '../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'

// Tabs row for WeatherPanelV2.
//
// Tab ids:
//   'global'       — always present, first slot
//   'microburst'   — standalone microburst authoring (Slice 5c)
//   'departure' / 'destination' — role string when pendingTabs has the
//                   role (user clicked +DEP but hasn't picked an airport);
//                   pivots to a client_id once addPatch fires
//   <client_id>    — a patch tab (departure/destination/custom with
//                   reserved patch_id)
//
// Slots are dynamic. For DEP/DEST:
//   patch exists        → DEP · EBBR tab (× closes via removePatch)
//   pendingTabs has role → DEP (…) pending tab (× closes via closePendingTab)
//   neither             → "+ DEP" chip (opens pending tab)
// Custom patches each get their own tab. "+" chip fires addPatch('custom',
// {lat, lon}) immediately with aircraft-position defaults.
//
// Accept / Discard are inline on the right. Dirty indicator shows when
// draft ≠ serverState (covers global scalars, cloud/wind layers, and
// patch override authoring — identity changes mirror to serverState on
// change and don't trigger dirty).

export default function WeatherV2Tabs() {
  const {
    activeTab, setActiveTab, draft, serverState,
    accept, discard, addPatch, removePatch,
    pendingTabs, openPendingTab, closePendingTab,
  } = useWeatherV2Store(useShallow(s => ({
    activeTab:       s.activeTab,
    setActiveTab:    s.setActiveTab,
    draft:           s.draft,
    serverState:     s.serverState,
    accept:          s.accept,
    discard:         s.discard,
    addPatch:        s.addPatch,
    removePatch:     s.removePatch,
    pendingTabs:     s.pendingTabs,
    openPendingTab:  s.openPendingTab,
    closePendingTab: s.closePendingTab,
  })))

  const isDirty = JSON.stringify(draft) !== JSON.stringify(serverState)

  const patches     = draft.patches ?? []
  const departure   = patches.find(p => p.role === 'departure')
  const destination = patches.find(p => p.role === 'destination')
  const customs     = patches.filter(p => p.role === 'custom')

  // +Custom fires addPatch immediately with aircraft-position defaults.
  // Omitting ground_elevation_m signals the backend to SRTM-resolve and
  // follow up via update_patch_identity once the service returns.
  const createCustomPatch = () => {
    const fdm = useSimStore.getState().fdm ?? {}
    addPatch('custom', {
      lat_deg: fdm.lat ?? 0,
      lon_deg: fdm.lon ?? 0,
    })
    // addPatch pivots activeTab to the new client_id itself.
  }

  return (
    <div style={{
      display: 'flex', alignItems: 'center', justifyContent: 'space-between',
      borderBottom: '1px solid #1e293b', marginBottom: 12, padding: '0 4px',
      flexWrap: 'wrap', gap: 6,
    }}>
      {/* Left — tab buttons */}
      <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 2 }}>
        <TabButton
          active={activeTab === 'global'}
          onClick={() => setActiveTab('global')}
        >GLOBAL</TabButton>

        {/* Departure slot: patch > pending > +chip */}
        {departure ? (
          <TabButton
            active={activeTab === departure.client_id}
            onClick={() => setActiveTab(departure.client_id)}
            onClose={() => removePatch(departure.client_id)}
          >DEP{departure.icao ? ` · ${departure.icao}` : ''}</TabButton>
        ) : pendingTabs.has('departure') ? (
          <TabButton
            active={activeTab === 'departure'}
            onClick={() => setActiveTab('departure')}
            onClose={() => closePendingTab('departure')}
          >DEP …</TabButton>
        ) : (
          <TabButton dimmed onClick={() => openPendingTab('departure')}>
            + DEP
          </TabButton>
        )}

        {/* Destination slot: patch > pending > +chip */}
        {destination ? (
          <TabButton
            active={activeTab === destination.client_id}
            onClick={() => setActiveTab(destination.client_id)}
            onClose={() => removePatch(destination.client_id)}
          >DEST{destination.icao ? ` · ${destination.icao}` : ''}</TabButton>
        ) : pendingTabs.has('destination') ? (
          <TabButton
            active={activeTab === 'destination'}
            onClick={() => setActiveTab('destination')}
            onClose={() => closePendingTab('destination')}
          >DEST …</TabButton>
        ) : (
          <TabButton dimmed onClick={() => openPendingTab('destination')}>
            + DEST
          </TabButton>
        )}

        {/* Custom patches */}
        {customs.map(c => (
          <TabButton
            key={c.client_id}
            active={activeTab === c.client_id}
            onClick={() => setActiveTab(c.client_id)}
            onClose={() => removePatch(c.client_id)}
          >{c.label || 'CUSTOM'}{c.icao && c.icao !== c.label ? ` · ${c.icao}` : ''}</TabButton>
        ))}

        {/* + Custom — fires addPatch immediately with aircraft pos */}
        <TabButton dimmed onClick={createCustomPatch}>+</TabButton>

        {/* Microburst — retained placeholder, pending 5c rework */}
        <TabButton
          active={activeTab === 'microburst'}
          onClick={() => setActiveTab('microburst')}
        >MICROBURST</TabButton>
      </div>

      {/* Right — Unsaved indicator + Discard + Accept */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, paddingRight: 4 }}>
        {isDirty && (
          <span style={{
            fontSize: 10, color: '#bc4fcb', fontFamily: 'monospace',
            textTransform: 'uppercase', letterSpacing: 1,
          }}>Unsaved</span>
        )}
        <ToolBtn disabled={!isDirty} onClick={discard} tone="neutral">Discard</ToolBtn>
        <ToolBtn disabled={!isDirty} onClick={accept}  tone="primary">Accept</ToolBtn>
      </div>
    </div>
  )
}

// A tab button. Three visual variants:
//   - active (teal bottom border, bold)
//   - normal (muted grey)
//   - dimmed (create chip — even more muted, dotted-ish treatment via
//     background + subtle border)
// onClose (custom-only) renders a small × that doesn't trigger the
// parent onClick.
function TabButton({ active, dimmed, onClick, onClose, children }) {
  const base = {
    padding: '10px 14px',
    background: 'transparent', border: 'none',
    borderBottom: `2px solid ${active ? '#39d0d8' : 'transparent'}`,
    color: active ? '#39d0d8' : (dimmed ? '#475569' : '#64748b'),
    fontFamily: 'monospace',
    fontSize: active ? 13 : 12,
    fontWeight: active ? 700 : 500,
    letterSpacing: 1, textTransform: 'uppercase',
    cursor: 'pointer', touchAction: 'manipulation',
    transition: 'color 80ms, border-color 80ms',
    display: 'inline-flex', alignItems: 'center', gap: 6,
  }
  return (
    <button
      type="button"
      onClick={onClick}
      onTouchEnd={(e) => { e.preventDefault(); onClick?.() }}
      style={base}
      onMouseEnter={(e) => {
        if (!active && !dimmed) e.currentTarget.style.color = '#94a3b8'
        if (dimmed)              e.currentTarget.style.color = '#94a3b8'
      }}
      onMouseLeave={(e) => {
        if (!active) e.currentTarget.style.color = dimmed ? '#475569' : '#64748b'
      }}
    >
      <span>{children}</span>
      {onClose && (
        <span
          role="button"
          onClick={(e) => { e.stopPropagation(); onClose() }}
          onTouchEnd={(e) => { e.stopPropagation(); e.preventDefault(); onClose() }}
          style={{
            fontSize: 14, color: '#475569',
            padding: '0 4px', marginLeft: 2,
            borderRadius: 2,
          }}
          onMouseEnter={(e) => { e.currentTarget.style.color = '#f87171' }}
          onMouseLeave={(e) => { e.currentTarget.style.color = '#475569' }}
        >×</span>
      )}
    </button>
  )
}

function ToolBtn({ disabled, onClick, tone, children }) {
  const isPrimary = tone === 'primary'
  return (
    <button
      type="button"
      disabled={disabled}
      onClick={onClick}
      onTouchEnd={(e) => { e.preventDefault(); if (!disabled) onClick?.() }}
      style={{
        height: 28, padding: `0 ${isPrimary ? 14 : 10}px`,
        background:  disabled ? '#111827' : (isPrimary ? 'rgba(188, 79, 203, 0.10)' : '#111827'),
        border: `1px solid ${disabled ? '#1e293b' : (isPrimary ? '#bc4fcb' : '#1e293b')}`,
        borderRadius: 3,
        color: disabled ? '#334155' : (isPrimary ? '#bc4fcb' : '#94a3b8'),
        fontFamily: 'monospace', fontSize: 11, fontWeight: 700,
        letterSpacing: 1, textTransform: 'uppercase',
        cursor: disabled ? 'not-allowed' : 'pointer',
        opacity: disabled ? 0.5 : 1,
        touchAction: 'manipulation',
      }}
    >{children}</button>
  )
}
