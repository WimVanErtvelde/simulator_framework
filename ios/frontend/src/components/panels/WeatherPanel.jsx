import { useState } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { PanelRow, SectionHeader, FullWidthBtn } from './PanelUtils'
import AirportSearch from '../ui/AirportSearch'
import NumpadPopup from '../ui/NumpadPopup'

const KT_TO_MS = 0.51444
const FT2M = 0.3048

// CIGI cloud type enum — short list covers 90% of training scenarios
const CLOUD_TYPES = [
  { id: 5,  label: 'Cirrus' },
  { id: 10, label: 'Stratus' },
  { id: 7,  label: 'Cumulus' },
  { id: 6,  label: 'Cumulonimbus' },
  { id: 8,  label: 'Nimbostratus' },
  { id: 9,  label: 'Stratocumulus' },
]

const PRECIP_TYPES = [
  { id: 0, label: 'None' },
  { id: 1, label: 'Rain' },
  { id: 2, label: 'Snow' },
  { id: 3, label: 'Sleet' },
]

// Runway condition = 2-tap model: category × severity → 0-15 index.
// DRY is a single bucket (no severity). Non-DRY categories span 3 severity
// levels in sequence: LIGHT / MEDIUM / MAX.
const RUNWAY_CATEGORIES = [
  { id: 'DRY',   label: 'DRY',    color: '#22c55e', baseIndex: 0,  noSeverity: true },
  { id: 'WET',   label: 'WET',    color: '#39d0d8', baseIndex: 1 },
  { id: 'WATER', label: 'WATER',  color: '#06b6d4', baseIndex: 4 },
  { id: 'SNOW',  label: 'SNOW',   color: '#f1f5f9', baseIndex: 7 },
  { id: 'ICE',   label: 'ICE',    color: '#93c5fd', baseIndex: 10 },
  { id: 'SN+IC', label: 'SN+IC',  color: '#c084fc', baseIndex: 13 },
]
const SEVERITIES = ['LIGHT', 'MEDIUM', 'MAX']   // offsets 0 / 1 / 2 from baseIndex

function runwayIndexFromCategorySeverity(categoryId, severity) {
  const cat = RUNWAY_CATEGORIES.find(c => c.id === categoryId) ?? RUNWAY_CATEGORIES[0]
  if (cat.noSeverity) return cat.baseIndex
  const sevIdx = Math.max(0, SEVERITIES.indexOf(severity))
  return cat.baseIndex + sevIdx
}

function runwayDescribe(index) {
  const idx = Math.max(0, Math.min(15, Number(index) || 0))
  if (idx === 0) return { categoryId: 'DRY', severity: null, label: 'DRY' }
  // Non-DRY: categories are contiguous WET(1-3) WATER(4-6) SNOW(7-9) ICE(10-12) SN+IC(13-15)
  const group = Math.floor((idx - 1) / 3)   // 0..4
  const offset = (idx - 1) % 3              // 0..2
  const cat = RUNWAY_CATEGORIES[1 + group]  // skip DRY
  const severity = SEVERITIES[offset]
  return { categoryId: cat.id, severity, label: `${cat.label} (${severity.toLowerCase()})` }
}

// Presets set atmosphere + cloud layers (OVC stratus = overcast, coverage 100)
const PRESETS = {
  VMC:       { vis: 10000, qnh: 1013, clouds: [] },
  'CAT I':   { vis: 800,   qnh: 1013, clouds: [{ cloud_type: 10, base_agl_ft: 200, thickness_ft: 500, coverage_pct: 100 }] },
  'CAT II':  { vis: 400,   qnh: 1013, clouds: [{ cloud_type: 10, base_agl_ft: 100, thickness_ft: 500, coverage_pct: 100 }] },
  'CAT III': { vis: 75,    qnh: 1013, clouds: [{ cloud_type: 10, base_agl_ft: 50,  thickness_ft: 500, coverage_pct: 100 }] },
}

const INTENSITY_MAP = { Light: 5.0, Moderate: 10.0, Severe: 15.0 }

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
  cursor: 'pointer', touchAction: 'manipulation',
}

// Numpad-backed text field (standalone; does not track a shared form dict)
function NumpadField({ label, value, placeholder, allowDecimal = false, hint = '', onChange, pending = false }) {
  const [open, setOpen] = useState(false)
  return (
    <label style={{ fontSize: 12, color: '#64748b', fontFamily: 'monospace', display: 'block' }}>{label}
      <div
        style={{
          ...inputBase, cursor: 'pointer',
          display: 'flex', alignItems: 'center',
          borderColor: pending ? '#bc4fcb' : '#1e293b',
          color: value !== '' ? '#e2e8f0' : '#475569',
        }}
        onClick={() => setOpen(true)}
      >
        {value !== '' ? value : placeholder}
      </div>
      {open && (
        <NumpadPopup
          label={label}
          hint={hint}
          value={value || ''}
          allowDecimal={allowDecimal}
          onSubmit={(v) => { onChange(v); setOpen(false) }}
          onCancel={() => setOpen(false)}
        />
      )}
    </label>
  )
}

// Pill button group
function PillGroup({ options, value, onChange, color = '#39d0d8' }) {
  return (
    <div style={{ display: 'grid', gridTemplateColumns: `repeat(${options.length}, 1fr)`, gap: 4 }}>
      {options.map(opt => {
        const active = value === opt.id
        return (
          <button key={opt.id}
            onClick={() => onChange(opt.id)}
            style={{
              ...neutralBtn, height: 32, fontSize: 11,
              background: active ? 'rgba(57, 208, 216, 0.10)' : '#111827',
              borderColor: active ? color : '#1e293b',
              color: active ? color : '#64748b',
            }}
          >{opt.label}</button>
        )
      })}
    </div>
  )
}

export default function WeatherPanel() {
  const { atmosphere, ws, wsConnected, microbursts, weatherStation, setWeatherStation, activeWeather } = useSimStore(useShallow(s => ({
    atmosphere: s.atmosphere, ws: s.ws, wsConnected: s.wsConnected,
    microbursts: s.microbursts ?? [],
    weatherStation: s.weatherStation,
    setWeatherStation: s.setWeatherStation,
    activeWeather: s.activeWeather,
  })))
  const activeCloudLayers = activeWeather?.cloudLayers ?? []

  // Atmosphere form
  const [atmo, setAtmo] = useState({ vis: '', qnh: '', oat: '' })
  const atmoUpdate = (k, v) => setAtmo(prev => ({ ...prev, [k]: v }))

  // Wind form (surface only for now)
  const [wind, setWind] = useState({ dir: '', spd: '', turb: '' })
  const windUpdate = (k, v) => setWind(prev => ({ ...prev, [k]: v }))

  // Cloud authoring form — single layer being built, committed via ADD button.
  // Active layers live in backend (activeWeather.cloudLayers from store).
  const CLOUD_FORM_DEFAULTS = { cloud_type: 7, base_agl_ft: '3000', thickness_ft: '2000', coverage_pct: '50' }
  const [cloudForm, setCloudForm] = useState(CLOUD_FORM_DEFAULTS)
  const cloudFormUpdate = (k, v) => setCloudForm(prev => ({ ...prev, [k]: v }))

  // Precipitation
  const [precip, setPrecip] = useState({ type: 0, rate: '' })

  // Runway condition — authoritative state lives on the backend; this local
  // state is only used to remember the LAST severity tapped so the UI doesn't
  // jump to MEDIUM when the backend echoes the same index back.
  const runwayIndex = Number(activeWeather?.runwayFriction ?? 0) || 0
  const runwayActive = runwayDescribe(runwayIndex)

  // Microburst form
  const [mbForm, setMbForm] = useState({
    bearing: '', distance: '', radius: '1000', intensity: 'Moderate',
  })
  const mbUpdate = (k, v) => setMbForm(prev => ({ ...prev, [k]: v }))

  const hasPending = (
    Object.values(atmo).some(v => v !== '') ||
    Object.values(wind).some(v => v !== '') ||
    precip.rate !== '' || precip.type !== 0
  )

  const sendRunwayIndex = (index) => {
    if (!ws || !wsConnected) return
    ws.send(JSON.stringify({ type: 'set_runway_condition', data: { index } }))
  }

  const selectRunwayCategory = (categoryId) => {
    const cat = RUNWAY_CATEGORIES.find(c => c.id === categoryId)
    if (!cat) return
    // DRY is a single bucket; non-DRY defaults to MEDIUM (per task spec),
    // except when the user re-taps the currently-active category — then keep
    // the current severity so repeated taps don't stomp a LIGHT/MAX selection.
    let severity = 'MEDIUM'
    if (!cat.noSeverity && runwayActive.categoryId === categoryId && runwayActive.severity) {
      severity = runwayActive.severity
    }
    sendRunwayIndex(runwayIndexFromCategorySeverity(categoryId, severity))
  }

  const selectRunwaySeverity = (severity) => {
    if (runwayActive.categoryId === 'DRY') return      // no-op on DRY
    sendRunwayIndex(runwayIndexFromCategorySeverity(runwayActive.categoryId, severity))
  }

  const sendAddCloud = (cloud) => {
    if (!ws || !wsConnected) return
    ws.send(JSON.stringify({ type: 'add_cloud_layer', data: cloud }))
  }

  const addCloudFromForm = () => {
    if (activeCloudLayers.length >= 3) return
    const thickness_ft = Number(cloudForm.thickness_ft) || 0
    const thickness_m = thickness_ft * FT2M
    sendAddCloud({
      cloud_type: Number(cloudForm.cloud_type),
      coverage_pct: Number(cloudForm.coverage_pct) || 0,
      base_agl_ft: Number(cloudForm.base_agl_ft) || 0,
      thickness_m,
      transition_band_m: thickness_m * 0.1,
      scud_enable: false,
      scud_frequency_pct: 0,
    })
    setCloudForm(CLOUD_FORM_DEFAULTS)
  }

  const applyPreset = (preset) => {
    setAtmo(prev => ({ ...prev, vis: String(preset.vis), qnh: String(preset.qnh) }))
    // Presets mutate cloud state immediately on the backend (no ACCEPT for clouds)
    if (!ws || !wsConnected) return
    ws.send(JSON.stringify({ type: 'clear_cloud_layers' }))
    for (const c of preset.clouds) {
      const thickness_m = Number(c.thickness_ft) * FT2M
      sendAddCloud({
        cloud_type: Number(c.cloud_type),
        coverage_pct: Number(c.coverage_pct) || 0,
        base_agl_ft: Number(c.base_agl_ft) || 0,
        thickness_m,
        transition_band_m: thickness_m * 0.1,
        scud_enable: false,
        scud_frequency_pct: 0,
      })
    }
  }

  const accept = () => {
    if (!ws || !wsConnected) return
    const data = {}
    if (atmo.oat) data.temperature_sl_k = Number(atmo.oat) + 273.15
    if (atmo.qnh) data.pressure_sl_pa = Number(atmo.qnh) * 100
    if (atmo.vis) data.visibility_m = Number(atmo.vis)
    if (wind.dir) data.wind_direction_deg = Number(wind.dir)
    if (wind.spd) data.wind_speed_ms = Number(wind.spd) * KT_TO_MS
    if (wind.turb) data.turbulence_severity = Number(wind.turb) / 100

    // Cloud layers are NOT sent via ACCEPT — they're managed by the ADD/remove/clear
    // handlers which write directly to the backend cache.

    if (precip.type !== 0 || precip.rate !== '') {
      data.precipitation_type = Number(precip.type)
      data.precipitation_rate = precip.rate ? Number(precip.rate) / 100 : 0
    }

    // Runway condition is NOT sent via ACCEPT — the RUNWAY CONDITION section
    // fires `set_runway_condition` immediately on each category/severity tap.

    ws.send(JSON.stringify({ type: 'set_weather', data }))

    // Clear pending form state (clouds are backend-owned — don't touch cloudForm here)
    setAtmo({ vis: '', qnh: '', oat: '' })
    setWind({ dir: '', spd: '', turb: '' })
    setPrecip({ type: 0, rate: '' })
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

  const removeActiveCloud = (index) => {
    if (!ws || !wsConnected) return
    ws.send(JSON.stringify({ type: 'remove_cloud_layer', data: { index } }))
  }

  const clearAllClouds = () => {
    if (!ws || !wsConnected) return
    ws.send(JSON.stringify({ type: 'clear_cloud_layers' }))
  }

  const cloudTypeName = (id) => {
    const match = CLOUD_TYPES.find(t => t.id === id)
    return match ? match.label : `Type ${id}`
  }

  const stationObj = weatherStation?.icao
    ? { icao: weatherStation.icao, name: weatherStation.icao, elevation_m: weatherStation.elevation_m }
    : null
  const stationElevFt = weatherStation?.elevation_m
    ? Math.round(weatherStation.elevation_m / FT2M) : null

  return (
    <div>
      {/* ── STATION ───────────────────────────────────────────────── */}
      <SectionHeader title="WEATHER STATION" />
      <AirportSearch
        value={stationObj}
        onSelect={(apt) => { if (apt) setWeatherStation(apt.icao) }}
        placeholder="Select station"
      />
      {weatherStation?.icao && (
        <div style={{
          fontSize: 11, color: '#64748b', fontFamily: 'monospace',
          marginTop: 6, textAlign: 'right',
        }}>
          Elev: {stationElevFt} ft
        </div>
      )}

      {/* ── CURRENT CONDITIONS ────────────────────────────────────── */}
      <SectionHeader title="CURRENT CONDITIONS" />
      <PanelRow label="QNH" value={atmosphere.qnhHpa.toFixed(1)} unit="hPa" />
      <PanelRow label="OAT" value={atmosphere.oatCelsius.toFixed(1)} unit="°C" />
      <PanelRow label="Wind" value={`${atmosphere.windDirDeg.toFixed(0)}°/${atmosphere.windSpeedKt.toFixed(0)}`} unit="kt" />
      <PanelRow label="Visibility" value={atmosphere.visibilityM.toFixed(0)} unit="m" />
      <PanelRow label="Turbulence" value={(atmosphere.turbulenceIntensity * 100).toFixed(0)} unit="%" />

      {/* ── ATMOSPHERE ────────────────────────────────────────────── */}
      <SectionHeader title="ATMOSPHERE" />
      <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
        <NumpadField label="Visibility (m)" value={atmo.vis}
          placeholder={String(atmosphere.visibilityM.toFixed(0))}
          hint="metres"
          onChange={(v) => atmoUpdate('vis', v)} pending={atmo.vis !== ''} />
        <NumpadField label="QNH (hPa)" value={atmo.qnh}
          placeholder={atmosphere.qnhHpa.toFixed(1)}
          allowDecimal hint="hPa"
          onChange={(v) => atmoUpdate('qnh', v)} pending={atmo.qnh !== ''} />
        <NumpadField label="OAT (°C)" value={atmo.oat}
          placeholder={atmosphere.oatCelsius.toFixed(1)}
          allowDecimal hint="°C"
          onChange={(v) => atmoUpdate('oat', v)} pending={atmo.oat !== ''} />
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 6, margin: '12px 0' }}>
        {Object.entries(PRESETS).map(([name, preset]) => (
          <button key={name}
            onClick={() => applyPreset(preset)}
            onTouchEnd={(e) => { e.preventDefault(); applyPreset(preset) }}
            style={neutralBtn}
          >{name}</button>
        ))}
      </div>

      {/* ── WIND LAYERS ───────────────────────────────────────────── */}
      <SectionHeader title="WIND LAYERS" />
      <div style={{ fontSize: 11, color: '#475569', fontFamily: 'monospace', marginBottom: 6 }}>
        SURFACE
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 8 }}>
        <NumpadField label="Dir (°)" value={wind.dir}
          placeholder={atmosphere.windDirDeg.toFixed(0)}
          hint="0–360"
          onChange={(v) => windUpdate('dir', v)} pending={wind.dir !== ''} />
        <NumpadField label="Spd (kt)" value={wind.spd}
          placeholder={atmosphere.windSpeedKt.toFixed(0)}
          hint="knots"
          onChange={(v) => windUpdate('spd', v)} pending={wind.spd !== ''} />
        <NumpadField label="Turb (%)" value={wind.turb}
          placeholder={(atmosphere.turbulenceIntensity * 100).toFixed(0)}
          hint="0–100"
          onChange={(v) => windUpdate('turb', v)} pending={wind.turb !== ''} />
      </div>

      {/* ── CLOUD LAYERS ──────────────────────────────────────────── */}
      <SectionHeader title="CLOUD LAYERS" />
      <div style={{
        background: '#0d1117', border: '1px solid #1e293b', borderRadius: 4,
        padding: 10, marginBottom: 8,
      }}>
        <div style={{ marginBottom: 8 }}>
          <div style={{ fontSize: 11, color: '#64748b', fontFamily: 'monospace', marginBottom: 4 }}>TYPE</div>
          <PillGroup options={CLOUD_TYPES} value={cloudForm.cloud_type}
            onChange={(v) => cloudFormUpdate('cloud_type', v)} />
        </div>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 8 }}>
          <NumpadField label="Base (ft AGL)" value={cloudForm.base_agl_ft} placeholder="3000"
            hint="feet AGL" onChange={(v) => cloudFormUpdate('base_agl_ft', v)} />
          <NumpadField label="Thick (ft)" value={cloudForm.thickness_ft} placeholder="2000"
            hint="feet" onChange={(v) => cloudFormUpdate('thickness_ft', v)} />
          <NumpadField label="Cover (%)" value={cloudForm.coverage_pct} placeholder="50"
            hint="0–100" onChange={(v) => cloudFormUpdate('coverage_pct', v)} />
        </div>
      </div>
      <button onClick={addCloudFromForm}
        disabled={activeCloudLayers.length >= 3}
        style={{
          ...neutralBtn, width: '100%', height: 36,
          opacity: activeCloudLayers.length >= 3 ? 0.4 : 1,
          cursor: activeCloudLayers.length >= 3 ? 'not-allowed' : 'pointer',
        }}
      >{activeCloudLayers.length >= 3 ? 'MAX 3 LAYERS' : '+ ADD CLOUD LAYER'}</button>

      {/* Active cloud layers (from backend after last ACCEPT) */}
      {activeCloudLayers.length > 0 && (
        <div style={{ marginTop: 10 }}>
          <div style={{
            fontSize: 11, color: '#64748b', fontFamily: 'monospace',
            marginBottom: 6, letterSpacing: 1,
          }}>ACTIVE</div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
            {activeCloudLayers.map((cl, i) => {
              const baseFt = Math.round(Number(cl.base_agl_ft) || 0)
              const thickFt = Math.round((Number(cl.thickness_m) || 0) / FT2M)
              return (
                <div key={i} style={{
                  display: 'flex', justifyContent: 'space-between', alignItems: 'center',
                  padding: '4px 8px', background: '#0d1117', borderRadius: 3,
                  border: '1px solid #39d0d833', fontSize: 11, fontFamily: 'monospace',
                }}>
                  <span style={{ color: '#39d0d8' }}>
                    CL#{i + 1} {cloudTypeName(cl.cloud_type)} {baseFt}ft/{thickFt}ft {Math.round(cl.coverage_pct || 0)}%
                  </span>
                  <button style={{
                    background: 'transparent', border: 'none', color: '#ff3b30',
                    fontFamily: 'monospace', fontSize: 14, cursor: 'pointer',
                    padding: '2px 8px', lineHeight: 1,
                  }} onClick={() => removeActiveCloud(i)}>×</button>
                </div>
              )
            })}
            {activeCloudLayers.length > 1 && (
              <FullWidthBtn
                label="CLEAR ALL CLOUDS"
                style={{ background: 'rgba(255, 59, 48, 0.08)', color: '#ff3b30', borderColor: '#ff3b3044' }}
                onClick={clearAllClouds}
              />
            )}
          </div>
        </div>
      )}

      {/* ── PRECIPITATION ─────────────────────────────────────────── */}
      <SectionHeader title="PRECIPITATION" />
      <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
        <div>
          <div style={{ fontSize: 11, color: '#64748b', fontFamily: 'monospace', marginBottom: 4 }}>TYPE</div>
          <PillGroup options={PRECIP_TYPES} value={precip.type}
            onChange={(v) => setPrecip(p => ({ ...p, type: v }))} />
        </div>
        <NumpadField label="Rate (%)" value={precip.rate} placeholder="0"
          hint="0–100" onChange={(v) => setPrecip(p => ({ ...p, rate: v }))}
          pending={precip.rate !== ''} />
      </div>

      {/* ── RUNWAY CONDITION ──────────────────────────────────────── */}
      <SectionHeader title="RUNWAY CONDITION" />
      <div style={{
        display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 6, marginBottom: 10,
      }}>
        {RUNWAY_CATEGORIES.map(cat => {
          const active = runwayActive.categoryId === cat.id
          return (
            <button key={cat.id}
              onClick={() => selectRunwayCategory(cat.id)}
              onTouchEnd={(e) => { e.preventDefault(); selectRunwayCategory(cat.id) }}
              style={{
                ...neutralBtn, height: 44, fontSize: 12,
                background: active ? `${cat.color}22` : '#111827',
                borderColor: active ? cat.color : '#1e293b',
                color: active ? cat.color : '#64748b',
              }}
            >{cat.label}</button>
          )
        })}
      </div>

      {runwayActive.categoryId !== 'DRY' && (
        <>
          <div style={{
            fontSize: 11, color: '#64748b', fontFamily: 'monospace',
            marginBottom: 4, letterSpacing: 1,
          }}>SEVERITY</div>
          <div style={{
            display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 6, marginBottom: 8,
          }}>
            {SEVERITIES.map(sev => {
              const active = runwayActive.severity === sev
              const cat = RUNWAY_CATEGORIES.find(c => c.id === runwayActive.categoryId)
              const color = cat?.color ?? '#39d0d8'
              return (
                <button key={sev}
                  onClick={() => selectRunwaySeverity(sev)}
                  onTouchEnd={(e) => { e.preventDefault(); selectRunwaySeverity(sev) }}
                  style={{
                    ...neutralBtn, height: 44, fontSize: 12,
                    background: active ? `${color}22` : '#111827',
                    borderColor: active ? color : '#1e293b',
                    color: active ? color : '#64748b',
                  }}
                >{sev}</button>
              )
            })}
          </div>
        </>
      )}

      <div style={{
        fontSize: 11, color: '#94a3b8', fontFamily: 'monospace',
        padding: '6px 8px', background: '#0d1117',
        border: '1px solid #1e293b', borderRadius: 3,
      }}>
        Active: <span style={{ color: '#e2e8f0' }}>{runwayActive.label}</span>
        <span style={{ color: '#475569' }}> — index {runwayIndex}</span>
      </div>

      {/* ── ACCEPT ────────────────────────────────────────────────── */}
      {hasPending && (
        <div style={{ marginTop: 14 }}>
          <FullWidthBtn
            label="ACCEPT"
            style={{ background: 'rgba(188, 79, 203, 0.12)', color: '#bc4fcb', borderColor: '#bc4fcb' }}
            onClick={accept}
          />
        </div>
      )}

      {/* ── MICROBURST ────────────────────────────────────────────── */}
      <SectionHeader title="MICROBURST" />
      <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8 }}>
          <NumpadField label="Bearing (°)" value={mbForm.bearing} placeholder="---"
            hint="0–360" onChange={(v) => mbUpdate('bearing', v)}
            pending={mbForm.bearing !== ''} />
          <NumpadField label="Distance (NM)" value={mbForm.distance} placeholder="---"
            allowDecimal hint="0.1–20" onChange={(v) => mbUpdate('distance', v)}
            pending={mbForm.distance !== ''} />
        </div>
        <NumpadField label="Radius (m)" value={mbForm.radius} placeholder="1000"
          hint="100–5000" onChange={(v) => mbUpdate('radius', v)}
          pending={mbForm.radius !== '1000'} />

        <div style={{ fontSize: 11, color: '#64748b', fontFamily: 'monospace' }}>INTENSITY</div>
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

        {microbursts.length > 0 && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, marginTop: 4 }}>
            {microbursts.map(mb => (
              <div key={mb.hazard_id} style={{
                display: 'flex', justifyContent: 'space-between', alignItems: 'center',
                padding: '4px 8px', background: '#0d1117', borderRadius: 3,
                border: '1px solid #f59e0b33', fontSize: 11, fontFamily: 'monospace',
              }}>
                <span style={{ color: '#f59e0b' }}>
                  MB#{mb.hazard_id} R={mb.core_radius_m}m λ={mb.intensity}
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
