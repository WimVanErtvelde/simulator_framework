import { useState } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { SectionHeader, FullWidthBtn } from './PanelUtils'

const neutralBtn = {
  height: 44, background: '#1c2333', border: '1px solid #1e293b', borderRadius: 3,
  color: '#64748b', fontSize: 12, fontFamily: 'monospace', fontWeight: 700,
  letterSpacing: 1, textTransform: 'uppercase',
  cursor: 'pointer', touchAction: 'manipulation', transition: 'opacity 0.15s',
}

const stepBtn = {
  width: 44, height: 44, background: '#1c2333', border: '1px solid #1e293b',
  borderRadius: 3, color: '#e2e8f0', fontSize: 18, fontFamily: 'monospace',
  cursor: 'pointer', touchAction: 'manipulation',
  display: 'flex', alignItems: 'center', justifyContent: 'center',
}

export default function TimePanel() {
  const { simTimeSec, sendCommand } = useSimStore(useShallow(s => ({
    simTimeSec: s.simTimeSec, sendCommand: s.sendCommand,
  })))
  const [hour, setHour] = useState(12)
  const [minute, setMinute] = useState(0)
  const [pending, setPending] = useState(false)

  const h = Math.floor(simTimeSec / 3600).toString().padStart(2, '0')
  const m = Math.floor((simTimeSec % 3600) / 60).toString().padStart(2, '0')
  const s = Math.floor(simTimeSec % 60).toString().padStart(2, '0')

  const btnGroupStyle = { display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 6, margin: '8px 0' }

  const setTime = (h, m) => { setHour(h); setMinute(m); setPending(true) }

  const seasons = [
    { label: 'WINTER', month: 1 },
    { label: 'SPRING', month: 4 },
    { label: 'SUMMER', month: 7 },
    { label: 'FALL', month: 10 },
  ]

  const timePresets = [
    { label: 'NIGHT', h: 2, m: 0 },
    { label: 'MORNING', h: 8, m: 0 },
    { label: 'AFTERNOON', h: 14, m: 0 },
    { label: 'EVENING', h: 20, m: 0 },
  ]

  const accept = () => {
    sendCommand(11, { hour, minute })
    setPending(false)
  }

  return (
    <div>
      <SectionHeader title="SIM TIME" />
      <div style={{
        fontSize: 28, fontWeight: 700, fontFamily: 'monospace',
        textAlign: 'center', padding: '12px 0', color: '#e2e8f0',
      }}>{h}:{m}:{s}</div>

      <SectionHeader title="SEASON" />
      <div style={btnGroupStyle}>
        {seasons.map((se) => (
          <button key={se.label} style={neutralBtn}
            onClick={() => sendCommand(12, { month: se.month })}
            onTouchEnd={(e) => { e.preventDefault(); sendCommand(12, { month: se.month }) }}
          >{se.label}</button>
        ))}
      </div>

      <SectionHeader title="TIME PRESETS" />
      <div style={btnGroupStyle}>
        {timePresets.map((tp) => (
          <button key={tp.label} style={neutralBtn}
            onClick={() => setTime(tp.h, tp.m)}
            onTouchEnd={(e) => { e.preventDefault(); setTime(tp.h, tp.m) }}
          >{tp.label}</button>
        ))}
      </div>

      <SectionHeader title="SET TIME" />
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 8 }}>
        <span style={{ color: '#64748b', fontSize: 13, width: 50, fontFamily: 'monospace', letterSpacing: 1 }}>HOUR</span>
        <button style={stepBtn} onClick={() => { setHour(h => Math.max(0, h - 1)); setPending(true) }}
          onTouchEnd={(e) => { e.preventDefault(); setHour(h => Math.max(0, h - 1)); setPending(true) }}>-</button>
        <span style={{ width: 40, textAlign: 'center', fontSize: 16, fontWeight: 700, fontFamily: 'monospace', color: '#e2e8f0' }}>{hour}</span>
        <button style={stepBtn} onClick={() => { setHour(h => Math.min(23, h + 1)); setPending(true) }}
          onTouchEnd={(e) => { e.preventDefault(); setHour(h => Math.min(23, h + 1)); setPending(true) }}>+</button>
      </div>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 12 }}>
        <span style={{ color: '#64748b', fontSize: 13, width: 50, fontFamily: 'monospace', letterSpacing: 1 }}>MIN</span>
        <button style={stepBtn} onClick={() => { setMinute(m => Math.max(0, m - 1)); setPending(true) }}
          onTouchEnd={(e) => { e.preventDefault(); setMinute(m => Math.max(0, m - 1)); setPending(true) }}>-</button>
        <span style={{ width: 40, textAlign: 'center', fontSize: 16, fontWeight: 700, fontFamily: 'monospace', color: '#e2e8f0' }}>{minute}</span>
        <button style={stepBtn} onClick={() => { setMinute(m => Math.min(59, m + 1)); setPending(true) }}
          onTouchEnd={(e) => { e.preventDefault(); setMinute(m => Math.min(59, m + 1)); setPending(true) }}>+</button>
      </div>

      {pending && (
        <FullWidthBtn
          label="ACCEPT"
          style={{ background: 'rgba(188, 79, 203, 0.12)', color: '#bc4fcb', borderColor: '#bc4fcb' }}
          onClick={accept}
        />
      )}
    </div>
  )
}
