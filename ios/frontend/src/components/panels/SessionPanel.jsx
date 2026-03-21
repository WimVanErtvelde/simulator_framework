import { useState } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { PanelRow, SectionHeader, FullWidthBtn } from './PanelUtils'

const inputBase = {
  width: '100%', height: 44, padding: '8px 12px',
  background: '#1c2333', border: '1px solid #1e293b', borderRadius: 3,
  color: '#e2e8f0', fontFamily: 'monospace', fontSize: 13,
  WebkitAppearance: 'none', outline: 'none',
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
      <label style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace', display: 'block', marginBottom: 12 }}>
        Instructor Name
        <input style={inputBase} value={form.instructorName}
          onChange={(e) => setForm(p => ({ ...p, instructorName: e.target.value }))} />
      </label>
      <label style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace', display: 'block', marginBottom: 12 }}>
        Pilot Name
        <input style={inputBase} value={form.pilotName}
          onChange={(e) => setForm(p => ({ ...p, pilotName: e.target.value }))} />
      </label>
      <label style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace', display: 'block', marginBottom: 12 }}>
        Session Name
        <input style={inputBase} value={form.sessionName}
          onChange={(e) => setForm(p => ({ ...p, sessionName: e.target.value }))} />
      </label>
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
