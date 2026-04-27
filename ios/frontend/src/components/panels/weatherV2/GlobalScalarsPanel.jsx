import { useShallow } from 'zustand/react/shallow'
import { useWeatherV2Store } from '../../../store/useWeatherV2Store'
import VisibilityField    from './fields/VisibilityField'
import TemperatureField   from './fields/TemperatureField'
import PressureField      from './fields/PressureField'
import HumidityField      from './fields/HumidityField'
import PrecipitationField from './fields/PrecipitationField'
import RunwayField        from './fields/RunwayField'

// Right column of WeatherPanelV2 Global tab. Atmospheric scalars + runway
// condition. Wind and cloud layers live in the middle MSL graph column.
//
// Field components accept values via props (Slice 5b-i). This panel hooks
// the store once, reads draft.global, and forwards each value + updater
// down. Fields don't import useWeatherV2Store directly — that lets them
// be reused in patch tab context (Slice 5b-iii) without the store shape
// leaking into their logic.
export default function GlobalScalarsPanel() {
  const {
    visibility_m, temperature_c, pressure_hpa, humidity_pct,
    precipitation_rate, precipitation_type, runway_condition_idx,
    updateDraft, setRunwayConditionIdx,
  } = useWeatherV2Store(useShallow(s => ({
    visibility_m:          s.draft.global.visibility_m,
    temperature_c:         s.draft.global.temperature_c,
    pressure_hpa:          s.draft.global.pressure_hpa,
    humidity_pct:          s.draft.global.humidity_pct,
    precipitation_rate:    s.draft.global.precipitation_rate,
    precipitation_type:    s.draft.global.precipitation_type,
    runway_condition_idx:  s.draft.global.runway_condition_idx ?? 0,
    updateDraft:           s.updateDraft,
    setRunwayConditionIdx: s.setRunwayConditionIdx,
  })))

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
        value={visibility_m}
        onChange={(v) => updateDraft(['global', 'visibility_m'], v)}
      />
      <TemperatureField
        value={temperature_c}
        onChange={(v) => updateDraft(['global', 'temperature_c'], v)}
      />
      <PressureField
        value={pressure_hpa}
        onChange={(v) => updateDraft(['global', 'pressure_hpa'], v)}
      />
      <HumidityField
        value={humidity_pct}
        onChange={(v) => updateDraft(['global', 'humidity_pct'], v)}
      />
      <PrecipitationField
        rate={precipitation_rate}
        type={precipitation_type}
        onChangeRate={(v) => updateDraft(['global', 'precipitation_rate'], v)}
        onChangeType={(v) => updateDraft(['global', 'precipitation_type'], v)}
      />
      <RunwayField
        label="Runway Condition (IG + FDM)"
        value={runway_condition_idx}
        onChange={setRunwayConditionIdx}
      />
    </div>
  )
}
