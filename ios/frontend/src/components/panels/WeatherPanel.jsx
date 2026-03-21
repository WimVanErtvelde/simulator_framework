import { useState } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { PanelRow, SectionHeader, FullWidthBtn } from './PanelUtils'

const PRESETS = {
  VMC:      { vis: 10000, qnh: 1013 },
  'IMC CAT I':  { vis: 800,  qnh: 1013 },
  'CAT II':     { vis: 400,  qnh: 1013 },
  'CAT III':    { vis: 75,   qnh: 1013 },
}

const inputBase = {
  width: '100%', height: 44, padding: '8px 12px',
  background: '#1c2333', border: '1px solid #1e293b',
  borderRadius: 3, color: '#e2e8f0', fontFamily: 'monospace', fontSize: 13,
  WebkitAppearance: 'none', outline: 'none',
}

const neutralBtn = {
  height: 36, background: '#1c2333', border: '1px solid #1e293b', borderRadius: 3,
  color: '#64748b', fontSize: 12, fontFamily: 'monospace', fontWeight: 700,
  letterSpacing: 1, textTransform: 'uppercase',
  cursor: 'pointer', touchAction: 'manipulation', transition: 'opacity 0.15s',
}

export default function WeatherPanel() {
  const { atmosphere, sendCommand } = useSimStore()
  const [form, setForm] = useState({
    vis: '', qnh: '', oat: '', windDir: '', windSpd: '',
  })

  const hasPending = Object.values(form).some(v => v !== '')

  const inputStyle = (field) => ({
    ...inputBase,
    borderColor: form[field] !== '' ? '#bc4fcb' : '#1e293b',
  })

  const update = (field, val) => setForm(prev => ({ ...prev, [field]: val }))

  const applyPreset = (preset) => {
    setForm(prev => ({
      ...prev,
      vis: String(preset.vis),
      qnh: String(preset.qnh),
    }))
  }

  const accept = () => {
    const data = {}
    if (form.vis) data.visibility_m = Number(form.vis)
    if (form.qnh) data.qnh_hpa = Number(form.qnh)
    if (form.oat) data.oat_celsius = Number(form.oat)
    if (form.windDir) data.wind_dir_deg = Number(form.windDir)
    if (form.windSpd) data.wind_speed_kt = Number(form.windSpd)
    sendCommand(10, data)
    setForm({ vis: '', qnh: '', oat: '', windDir: '', windSpd: '' })
  }

  return (
    <div>
      <SectionHeader title="CURRENT CONDITIONS" />
      <PanelRow label="QNH" value={atmosphere.qnhHpa.toFixed(1)} unit="hPa" />
      <PanelRow label="OAT" value={atmosphere.oatCelsius.toFixed(1)} unit="°C" />
      <PanelRow label="Wind Dir" value={atmosphere.windDirDeg.toFixed(0)} unit="°" />
      <PanelRow label="Wind Spd" value={atmosphere.windSpeedKt.toFixed(0)} unit="kt" />
      <PanelRow label="Visibility" value={atmosphere.visibilityM.toFixed(0)} unit="m" />

      <SectionHeader title="SET CONDITIONS" />
      <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
        <label style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace', display: 'block', marginBottom: 4 }}>Visibility (m)
          <input type="number" style={inputStyle('vis')} value={form.vis} placeholder={atmosphere.visibilityM} onChange={e => update('vis', e.target.value)} />
        </label>
        <label style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace', display: 'block', marginBottom: 4 }}>QNH (hPa)
          <input type="number" style={inputStyle('qnh')} value={form.qnh} placeholder={atmosphere.qnhHpa} onChange={e => update('qnh', e.target.value)} />
        </label>
        <label style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace', display: 'block', marginBottom: 4 }}>OAT (°C)
          <input type="number" style={inputStyle('oat')} value={form.oat} placeholder={atmosphere.oatCelsius} onChange={e => update('oat', e.target.value)} />
        </label>
        <label style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace', display: 'block', marginBottom: 4 }}>Wind Dir (°)
          <input type="number" style={inputStyle('windDir')} value={form.windDir} placeholder={atmosphere.windDirDeg} onChange={e => update('windDir', e.target.value)} />
        </label>
        <label style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace', display: 'block', marginBottom: 4 }}>Wind Spd (kt)
          <input type="number" style={inputStyle('windSpd')} value={form.windSpd} placeholder={atmosphere.windSpeedKt} onChange={e => update('windSpd', e.target.value)} />
        </label>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 6, margin: '12px 0' }}>
        {Object.entries(PRESETS).map(([name, preset]) => (
          <button
            key={name}
            onClick={() => applyPreset(preset)}
            onTouchEnd={(e) => { e.preventDefault(); applyPreset(preset) }}
            style={neutralBtn}
          >{name}</button>
        ))}
      </div>

      {hasPending && (
        <FullWidthBtn
          label="ACCEPT"
          style={{ background: 'rgba(188, 79, 203, 0.12)', color: '#bc4fcb', borderColor: '#bc4fcb' }}
          onClick={accept}
        />
      )}
    </div>
  )
}
