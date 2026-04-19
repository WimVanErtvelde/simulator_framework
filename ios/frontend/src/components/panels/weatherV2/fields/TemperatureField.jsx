import { useSimStore } from '../../../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'
import { isaTemperatureAt } from '../weatherUnits'
import { M_TO_FT } from '../graph/mslScale'
import { fieldBox, fieldHeader, fieldLabel, fieldValue, slider,
         COLOR_TEXT_MUTED } from './fieldStyles'
import OverridePill from './OverridePill'

const READOUT_VALUE_COLOR = '#94a3b8'
const K_OFFSET = 273.15

// Authors sea-level temperature. "At aircraft" readout resolves the
// authored SL value to the aircraft's current position via the live
// atmosphere broadcast (always visible — display-only, independent of
// override state). Station readout (dormant on Global) resolves the
// authored SL value to the station elevation via ISA lapse.
export default function TemperatureField({
  value,
  onChange,
  showOverrideToggle  = false,
  overrideEnabled     = true,
  onToggleOverride    = () => {},
  showStationReadout  = false,
  stationIcao         = '',
  stationElevM        = 0,
}) {
  const { oatAtAircraftC, altFtAgl } = useSimStore(useShallow(s => ({
    oatAtAircraftC: s.atmosphere?.oatCelsius,
    altFtAgl:       s.fdm?.altFtAgl ?? 0,
  })))

  const disabled      = !overrideEnabled
  const aircraftAglFt = Math.round(altFtAgl)

  // Resolve authored SL temperature to station elevation via ISA lapse.
  // isaTemperatureAt returns Kelvin; convert back to °C for display.
  const stationTempC = showStationReadout && stationIcao
    ? isaTemperatureAt(value + K_OFFSET, stationElevM) - K_OFFSET
    : null

  return (
    <div style={{ ...fieldBox, opacity: disabled ? 0.4 : 1 }}>
      <div style={fieldHeader}>
        <span style={fieldLabel}>Sea Level Temp</span>
        {showOverrideToggle && (
          <OverridePill enabled={overrideEnabled} onToggle={onToggleOverride} />
        )}
        <span style={fieldValue}>{value.toFixed(0)} °C</span>
      </div>
      <input
        type="range"
        min={-100} max={60} step={1}
        value={value}
        disabled={disabled}
        onChange={(e) => onChange(Number(e.target.value))}
        style={slider}
      />

      {Number.isFinite(oatAtAircraftC) && (
        <div style={{
          fontSize: 11, fontFamily: 'monospace', lineHeight: 1.4,
          color: COLOR_TEXT_MUTED,
        }}>
          At aircraft ({aircraftAglFt.toLocaleString()} ft AGL):{' '}
          <span style={{ color: READOUT_VALUE_COLOR }}>
            {oatAtAircraftC.toFixed(1)} °C
          </span>
        </div>
      )}

      {stationTempC !== null && (
        <div style={{
          fontSize: 11, fontFamily: 'monospace', lineHeight: 1.4,
          color: COLOR_TEXT_MUTED,
        }}>
          At {stationIcao} ({Math.round(stationElevM * M_TO_FT).toLocaleString()} ft):{' '}
          <span style={{ color: READOUT_VALUE_COLOR }}>
            {stationTempC.toFixed(1)} °C
          </span>
        </div>
      )}
    </div>
  )
}
