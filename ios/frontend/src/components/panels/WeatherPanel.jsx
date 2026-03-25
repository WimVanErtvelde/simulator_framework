import { useState } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { PanelRow, SectionHeader, FullWidthBtn } from './PanelUtils'
import NumpadPopup from '../ui/NumpadPopup'

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

function WeatherField({ label, field, form, update, placeholder, inputStyle, allowDecimal = false, hint = '' }) {
  const [open, setOpen] = useState(false)
  return (
    <label style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace', display: 'block', marginBottom: 4 }}>{label}
      <div
        style={{
          ...inputStyle(field), cursor: 'pointer',
          display: 'flex', alignItems: 'center',
          color: form[field] !== '' ? '#e2e8f0' : '#475569',
        }}
        onClick={() => setOpen(true)}
      >
        {form[field] !== '' ? form[field] : placeholder}
      </div>
      {open && (
        <NumpadPopup
          label={label}
          hint={hint}
          value={form[field]}
          allowDecimal={allowDecimal}
          onSubmit={(v) => { update(field, v); setOpen(false) }}
          onCancel={() => setOpen(false)}
        />
      )}
    </label>
  )
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
        <WeatherField label="Visibility (m)" field="vis" form={form} update={update}
          placeholder={atmosphere.visibilityM} inputStyle={inputStyle} hint="metres" />
        <WeatherField label="QNH (hPa)" field="qnh" form={form} update={update}
          placeholder={atmosphere.qnhHpa} inputStyle={inputStyle} allowDecimal hint="hPa" />
        <WeatherField label="OAT (\u00B0C)" field="oat" form={form} update={update}
          placeholder={atmosphere.oatCelsius} inputStyle={inputStyle} allowDecimal hint="\u00B0C" />
        <WeatherField label="Wind Dir (\u00B0)" field="windDir" form={form} update={update}
          placeholder={atmosphere.windDirDeg} inputStyle={inputStyle} hint="0\u2013360" />
        <WeatherField label="Wind Spd (kt)" field="windSpd" form={form} update={update}
          placeholder={atmosphere.windSpeedKt} inputStyle={inputStyle} hint="knots" />
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
