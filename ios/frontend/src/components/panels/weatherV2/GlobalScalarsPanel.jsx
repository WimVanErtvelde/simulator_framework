import VisibilityField    from './fields/VisibilityField'
import TemperatureField   from './fields/TemperatureField'
import PressureField      from './fields/PressureField'
import HumidityField      from './fields/HumidityField'
import PrecipitationField from './fields/PrecipitationField'
import RunwayField        from './fields/RunwayField'

// Right column of WeatherPanelV2 Global tab. Atmospheric scalars + runway
// condition. Wind and cloud layers live in the middle MSL graph column.
export default function GlobalScalarsPanel() {
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
      <VisibilityField />
      <TemperatureField />
      <PressureField />
      <HumidityField />
      <PrecipitationField />
      <RunwayField />
    </div>
  )
}
