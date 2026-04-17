import { useState } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { PanelRow, SectionHeader, FullWidthBtn } from './PanelUtils'
import NumpadPopup from '../ui/NumpadPopup'

const PRESETS = {
  VMC:      { vis: 10000, qnh: 1013 },
  'IMC CAT I':  { vis: 800,  qnh: 1013 },
  'CAT II':     { vis: 400,  qnh: 1013 },
  'CAT III':    { vis: 75,   qnh: 1013 },
}

const KT_TO_MS = 0.51444

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

const stubStyle = {
  padding: '12px 8px', margin: '8px 0', textAlign: 'center',
  background: '#0d1117', border: '1px dashed #1e293b', borderRadius: 4,
  color: '#475569', fontSize: 11, fontFamily: 'monospace',
}

const INTENSITY_MAP = {
  Light: 5.0,
  Moderate: 10.0,
  Severe: 15.0,
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
  const { atmosphere, ws, wsConnected, microbursts } = useSimStore(useShallow(s => ({
    atmosphere: s.atmosphere, ws: s.ws, wsConnected: s.wsConnected,
    microbursts: s.microbursts ?? [],
  })))
  const [form, setForm] = useState({
    vis: '', qnh: '', oat: '', windDir: '', windSpd: '', turb: '',
  })
  const [mbForm, setMbForm] = useState({
    bearing: '', distance: '', radius: '1000', intensity: 'Moderate',
  })

  const hasPending = Object.values(form).some(v => v !== '')

  const inputStyle = (field) => ({
    ...inputBase,
    borderColor: form[field] !== '' ? '#bc4fcb' : '#1e293b',
  })

  const mbInputStyle = (field) => ({
    ...inputBase,
    borderColor: mbForm[field] !== '' ? '#f59e0b' : '#1e293b',
  })

  const update = (field, val) => setForm(prev => ({ ...prev, [field]: val }))
  const mbUpdate = (field, val) => setMbForm(prev => ({ ...prev, [field]: val }))

  const applyPreset = (preset) => {
    setForm(prev => ({
      ...prev,
      vis: String(preset.vis),
      qnh: String(preset.qnh),
    }))
  }

  const accept = () => {
    if (!ws || !wsConnected) return
    const data = {}
    if (form.oat) data.temperature_sl_k = Number(form.oat) + 273.15
    if (form.qnh) data.pressure_sl_pa = Number(form.qnh) * 100
    if (form.vis) data.visibility_m = Number(form.vis)
    if (form.windDir) data.wind_direction_deg = Number(form.windDir)
    if (form.windSpd) data.wind_speed_ms = Number(form.windSpd) * KT_TO_MS
    if (form.turb) data.turbulence_severity = Number(form.turb) / 100

    ws.send(JSON.stringify({ type: 'set_weather', data }))
    setForm({ vis: '', qnh: '', oat: '', windDir: '', windSpd: '', turb: '' })
  }

  const activateMicroburst = () => {
    if (!ws || !wsConnected) return
    if (microbursts.length >= 4) return
    if (!mbForm.bearing || !mbForm.distance) return

    ws.send(JSON.stringify({
      type: 'activate_microburst',
      data: {
        bearing_deg: Number(mbForm.bearing),
        distance_nm: Number(mbForm.distance),
        intensity: INTENSITY_MAP[mbForm.intensity] ?? 10.0,
        core_radius_m: Number(mbForm.radius) || 1000,
        shaft_altitude_m: 300.0,
      }
    }))
    setMbForm(prev => ({ ...prev, bearing: '', distance: '' }))
  }

  const clearMicroburst = (hazardId) => {
    if (!ws || !wsConnected) return
    ws.send(JSON.stringify({ type: 'clear_microburst', data: { hazard_id: hazardId } }))
  }

  const clearAllMicrobursts = () => {
    if (!ws || !wsConnected) return
    ws.send(JSON.stringify({ type: 'clear_all_microbursts' }))
  }

  return (
    <div>
      <SectionHeader title="CURRENT CONDITIONS" />
      <PanelRow label="QNH" value={atmosphere.qnhHpa.toFixed(1)} unit="hPa" />
      <PanelRow label="OAT" value={atmosphere.oatCelsius.toFixed(1)} unit="°C" />
      <PanelRow label="Wind Dir" value={atmosphere.windDirDeg.toFixed(0)} unit="°" />
      <PanelRow label="Wind Spd" value={atmosphere.windSpeedKt.toFixed(0)} unit="kt" />
      <PanelRow label="Visibility" value={atmosphere.visibilityM.toFixed(0)} unit="m" />
      <PanelRow label="Turbulence" value={(atmosphere.turbulenceIntensity * 100).toFixed(0)} unit="%" />

      <SectionHeader title="SET CONDITIONS" />
      <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
        <WeatherField label="Visibility (m)" field="vis" form={form} update={update}
          placeholder={atmosphere.visibilityM} inputStyle={inputStyle} hint="metres" />
        <WeatherField label="QNH (hPa)" field="qnh" form={form} update={update}
          placeholder={atmosphere.qnhHpa} inputStyle={inputStyle} allowDecimal hint="hPa" />
        <WeatherField label="OAT (°C)" field="oat" form={form} update={update}
          placeholder={atmosphere.oatCelsius} inputStyle={inputStyle} allowDecimal hint="°C" />
        <WeatherField label="Wind Dir (°)" field="windDir" form={form} update={update}
          placeholder={atmosphere.windDirDeg} inputStyle={inputStyle} hint="0–360" />
        <WeatherField label="Wind Spd (kt)" field="windSpd" form={form} update={update}
          placeholder={atmosphere.windSpeedKt} inputStyle={inputStyle} hint="knots" />
        <WeatherField label="Turbulence (%)" field="turb" form={form} update={update}
          placeholder={(atmosphere.turbulenceIntensity * 100).toFixed(0)} inputStyle={inputStyle} hint="0–100" />
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

      <div style={stubStyle}>CLOUD LAYERS — coming soon</div>
      <div style={stubStyle}>PRECIPITATION — coming soon</div>

      {/* ── MICROBURST ───────────────────────────────────────────── */}
      <SectionHeader title="MICROBURST" />
      <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8 }}>
          <WeatherField label="Bearing (°)" field="bearing" form={mbForm} update={mbUpdate}
            placeholder="---" inputStyle={mbInputStyle} hint="0–360" />
          <WeatherField label="Distance (NM)" field="distance" form={mbForm} update={mbUpdate}
            placeholder="---" inputStyle={mbInputStyle} allowDecimal hint="0.1–20" />
        </div>
        <WeatherField label="Radius (m)" field="radius" form={mbForm} update={mbUpdate}
          placeholder="1000" inputStyle={mbInputStyle} hint="100–5000" />

        <div style={{ fontSize: 11, color: '#64748b', fontFamily: 'monospace', marginBottom: 2 }}>
          INTENSITY
        </div>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 4 }}>
          {Object.keys(INTENSITY_MAP).map(label => (
            <button key={label} style={{
              ...neutralBtn, height: 32, fontSize: 11,
              background: mbForm.intensity === label ? '#1c2333' : '#111827',
              borderColor: mbForm.intensity === label ? '#f59e0b' : '#1e293b',
              color: mbForm.intensity === label ? '#f59e0b' : '#64748b',
            }}
              onClick={() => mbUpdate('intensity', label)}
            >{label}</button>
          ))}
        </div>

        <FullWidthBtn
          label={microbursts.length >= 4 ? 'MAX 4 ACTIVE' : 'ACTIVATE'}
          style={{
            background: microbursts.length >= 4 ? '#1c2333' : 'rgba(245, 158, 11, 0.12)',
            color: microbursts.length >= 4 ? '#475569' : '#f59e0b',
            borderColor: microbursts.length >= 4 ? '#1e293b' : '#f59e0b',
            cursor: microbursts.length >= 4 ? 'not-allowed' : 'pointer',
          }}
          onClick={activateMicroburst}
        />

        {/* Active microbursts */}
        {microbursts.length > 0 && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, marginTop: 4 }}>
            {microbursts.map(mb => (
              <div key={mb.hazard_id} style={{
                display: 'flex', justifyContent: 'space-between', alignItems: 'center',
                padding: '4px 8px', background: '#0d1117', borderRadius: 3,
                border: '1px solid #f59e0b33', fontSize: 11, fontFamily: 'monospace',
              }}>
                <span style={{ color: '#f59e0b' }}>
                  MB#{mb.hazard_id} R={mb.core_radius_m}m {'\u03BB'}={mb.intensity}
                </span>
                <button style={{
                  background: 'transparent', border: 'none', color: '#ff3b30',
                  fontFamily: 'monospace', fontSize: 11, cursor: 'pointer',
                  padding: '2px 6px',
                }} onClick={() => clearMicroburst(mb.hazard_id)}>CLEAR</button>
              </div>
            ))}
            {microbursts.length > 1 && (
              <FullWidthBtn
                label="CLEAR ALL"
                style={{ background: 'rgba(255, 59, 48, 0.08)', color: '#ff3b30', borderColor: '#ff3b3044' }}
                onClick={clearAllMicrobursts}
              />
            )}
          </div>
        )}
      </div>
    </div>
  )
}
