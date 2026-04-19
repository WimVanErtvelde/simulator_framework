import { useSimStore } from '../../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { useWeatherV2Store } from '../../../../store/useWeatherV2Store'
import { isaReferenceAt, isaTemperatureAt, K_OFFSET } from '../weatherUnits'
import { M_TO_FT } from '../graph/mslScale'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider,
         COLOR_TEXT_MUTED, COLOR_TEXT_DIM } from './fieldStyles'

const READOUT_VALUE_COLOR = '#94a3b8'
const ISA_WARM_COLOR      = '#f59e0b'
const ISA_COLD_COLOR      = '#39d0d8'

// Authors sea-level temperature. Two derived readout rows under the
// slider resolve the authored SL value to a station-level and an
// aircraft-position value — makes clear that X-Plane interpolates the
// authored sea-level temp to the aircraft's altitude via ISA lapse.
export default function TemperatureField() {
  const temperature_c = useWeatherV2Store(s => s.draft.global.temperature_c)
  const updateDraft   = useWeatherV2Store(s => s.updateDraft)

  const { stationIcao, stationElevM, oatAtAircraftC, altFtAgl } =
    useSimStore(useShallow(s => ({
      stationIcao:    s.activeWeather?.stationIcao ?? '',
      stationElevM:   s.activeWeather?.stationElevationM ?? 0,
      oatAtAircraftC: s.atmosphere?.oatCelsius,   // may be undefined
      altFtAgl:       s.fdm?.altFtAgl ?? 0,
    })))

  const slTempK       = temperature_c + K_OFFSET
  const stationTempK  = isaTemperatureAt(slTempK, stationElevM)
  const stationTempC  = stationTempK - K_OFFSET
  const stationIsaK   = isaReferenceAt(stationElevM)
  const isaDeviationC = stationTempK - stationIsaK   // delta same in K and °C
  const stationElevFt = Math.round(stationElevM * M_TO_FT)
  const aircraftAglFt = Math.round(altFtAgl)

  const isaColor = Math.abs(isaDeviationC) < 0.5
    ? COLOR_TEXT_MUTED
    : (isaDeviationC >= 0 ? ISA_WARM_COLOR : ISA_COLD_COLOR)

  return (
    <div style={fieldBox}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>Sea Level Temp</span>
        <span style={fieldValue}>{temperature_c.toFixed(0)} °C</span>
      </div>
      <input
        type="range"
        min={-100} max={60} step={1}
        value={temperature_c}
        onChange={(e) => updateDraft(['global', 'temperature_c'], Number(e.target.value))}
        style={slider}
      />

      <div style={{
        fontSize: 11, fontFamily: 'monospace', lineHeight: 1.4,
        color: COLOR_TEXT_MUTED,
      }}>
        {stationIcao ? (
          <div>
            At {stationIcao} ({stationElevFt.toLocaleString()} ft):{' '}
            <span style={{ color: READOUT_VALUE_COLOR }}>
              {stationTempC.toFixed(1)} °C
            </span>{' '}
            <span style={{ color: isaColor }}>
              ISA {isaDeviationC >= 0 ? '+' : ''}{isaDeviationC.toFixed(1)} °C
            </span>
          </div>
        ) : (
          <div style={{ color: COLOR_TEXT_DIM }}>
            At station: (no station set)
          </div>
        )}
        {Number.isFinite(oatAtAircraftC) && (
          <div>
            At aircraft ({aircraftAglFt.toLocaleString()} ft AGL):{' '}
            <span style={{ color: READOUT_VALUE_COLOR }}>
              {oatAtAircraftC.toFixed(1)} °C
            </span>
          </div>
        )}
      </div>
    </div>
  )
}
