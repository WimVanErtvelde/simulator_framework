import { useState } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { PanelRow, SectionHeader, FullWidthBtn } from './PanelUtils'
import KeyboardPopup from '../ui/KeyboardPopup'

const inputBase = {
  width: '100%', height: 44, padding: '8px 12px',
  background: '#1c2333', border: '1px solid #1e293b', borderRadius: 3,
  color: '#e2e8f0', fontFamily: 'monospace', fontSize: 13,
  WebkitAppearance: 'none', outline: 'none',
}

function SessionField({ label, value, onChange }) {
  const [open, setOpen] = useState(false)
  return (
    <label style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace', display: 'block', marginBottom: 12 }}>
      {label}
      <div
        style={{
          ...inputBase, cursor: 'pointer',
          display: 'flex', alignItems: 'center',
          color: value ? '#e2e8f0' : '#475569',
        }}
        onClick={() => setOpen(true)}
      >
        {value || `Enter ${label.toLowerCase()}...`}
      </div>
      {open && (
        <KeyboardPopup
          label={label}
          value={value}
          onSubmit={(v) => { onChange(v); setOpen(false) }}
          onCancel={() => setOpen(false)}
        />
      )}
    </label>
  )
}

export default function SessionPanel() {
  const { session, wsConnected, wsReconnectCount } = useSimStore()
  const [form, setForm] = useState({
    instructorName: session.instructorName,
    pilotName: session.pilotName,
    sessionName: session.name,
  })

  const save = () => {
    useSimStore.setState({
      session: {
        name: form.sessionName,
        pilotName: form.pilotName,
        instructorName: form.instructorName,
      }
    })
  }

  return (
    <div>
      <SectionHeader title="SESSION INFO" />
      <SessionField label="Instructor Name" value={form.instructorName}
        onChange={(v) => setForm(p => ({ ...p, instructorName: v }))} />
      <SessionField label="Pilot Name" value={form.pilotName}
        onChange={(v) => setForm(p => ({ ...p, pilotName: v }))} />
      <SessionField label="Session Name" value={form.sessionName}
        onChange={(v) => setForm(p => ({ ...p, sessionName: v }))} />
      <FullWidthBtn
        label="SAVE"
        style={{ marginBottom: 16 }}
        onClick={save}
      />

      <SectionHeader title="CONNECTION" />
      <PanelRow label="Backend URL" value="ws://localhost:8080/ws" />
      <PanelRow label="Status" value={wsConnected ? 'LIVE' : `DISC (${wsReconnectCount})`} highlight={wsConnected} />
      <PanelRow label="WS State" value={wsConnected ? 'OPEN' : 'CLOSED'} />
    </div>
  )
}
