import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import VisibilityField    from './fields/VisibilityField'
import TemperatureField   from './fields/TemperatureField'
import PressureField      from './fields/PressureField'
import HumidityField      from './fields/HumidityField'
import PrecipitationField from './fields/PrecipitationField'
import RunwayField        from './fields/RunwayField'

// Right column of a patch tab. Mirrors GlobalScalarsPanel shape but each
// field is wired through the per-field override machinery — a pink
// OVERRIDE pill flips overrideEnabled, and when false the field renders
// disabled at 0.4 opacity (the "inherit from Global" affordance).
//
// Temperature + Pressure also activate their station readout block,
// showing the authored SL value resolved to the patch's airport elevation
// via ISA lapse (helpers live in weatherUnits.js).
//
// Note on deferrals (per Slice 5b-ii): humidity, pressure, and runway
// override flags exist in the draft but the wire doesn't carry them yet.
// UI still authors them so 5b-iv lands without UI churn.
export default function PatchScalarsPanel({ patch }) {
  const updatePatch    = useWeatherV2Store(s => s.updatePatch)
  const toggleOverride = useWeatherV2Store(s => s.toggleOverride)

  const cid = patch.client_id

  return (
    <div style={{
      padding: 12,
      background: '#0d1117',
      border: '1px solid #1e293b',
      borderRadius: 3,
      height: '100%', minHeight: 0,
      overflowY: 'auto',
      display: 'flex', flexDirection: 'column', gap: 10,
    }}>
      <VisibilityField
        value={patch.visibility_m}
        onChange={(v) => updatePatch(cid, { visibility_m: v })}
        showOverrideToggle
        overrideEnabled={patch.override_visibility}
        onToggleOverride={(en) => toggleOverride(cid, 'visibility', en)}
      />
      <TemperatureField
        value={patch.temperature_c}
        onChange={(v) => updatePatch(cid, { temperature_c: v })}
        showOverrideToggle
        overrideEnabled={patch.override_temperature}
        onToggleOverride={(en) => toggleOverride(cid, 'temperature', en)}
        showStationReadout
        stationIcao={patch.icao}
        stationElevM={patch.ground_elevation_m}
      />
      <PressureField
        value={patch.pressure_hpa}
        onChange={(v) => updatePatch(cid, { pressure_hpa: v })}
        showOverrideToggle
        overrideEnabled={patch.override_pressure}
        onToggleOverride={(en) => toggleOverride(cid, 'pressure', en)}
        showStationReadout
        stationIcao={patch.icao}
        stationElevM={patch.ground_elevation_m}
      />
      <HumidityField
        value={patch.humidity_pct}
        onChange={(v) => updatePatch(cid, { humidity_pct: v })}
        showOverrideToggle
        overrideEnabled={patch.override_humidity}
        onToggleOverride={(en) => toggleOverride(cid, 'humidity', en)}
      />
      <PrecipitationField
        rate={patch.precipitation_rate}
        type={patch.precipitation_type}
        onChangeRate={(v) => updatePatch(cid, { precipitation_rate: v })}
        onChangeType={(v) => updatePatch(cid, { precipitation_type: v })}
        showOverrideToggle
        overrideEnabled={patch.override_precipitation}
        onToggleOverride={(en) => toggleOverride(cid, 'precipitation', en)}
      />
      <RunwayField
        value={patch.runway_friction}
        onChange={(v) => updatePatch(cid, { runway_friction: v })}
        showOverrideToggle
        overrideEnabled={patch.override_runway}
        onToggleOverride={(en) => toggleOverride(cid, 'runway', en)}
      />
    </div>
  )
}
