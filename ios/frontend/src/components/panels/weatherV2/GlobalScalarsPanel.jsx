import VisibilityField    from './fields/VisibilityField'
import TemperatureField   from './fields/TemperatureField'
import PressureField      from './fields/PressureField'
import HumidityField      from './fields/HumidityField'
import PrecipitationField from './fields/PrecipitationField'
import RunwayField        from './fields/RunwayField'

// Global atmospheric scalars authoring. Slice 5a-ii.
// Wind and cloud layers live in their own columns (5a-iii / 5a-iv).
export default function GlobalScalarsPanel() {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
      <VisibilityField />
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
        <TemperatureField />
        <PressureField />
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
        <HumidityField />
        <PrecipitationField />
      </div>
      <RunwayField />
    </div>
  )
}
