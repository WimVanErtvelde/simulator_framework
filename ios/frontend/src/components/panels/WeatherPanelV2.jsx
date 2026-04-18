import { useWeatherV2Store } from '../../store/useWeatherV2Store'
import WeatherV2Header from './weatherV2/WeatherV2Header'
import WeatherV2Tabs   from './weatherV2/WeatherV2Tabs'
import GlobalTab       from './weatherV2/GlobalTab'
import PatchesTab      from './weatherV2/PatchesTab'
import MicroburstTab   from './weatherV2/MicroburstTab'

// New weather authoring UI. Coexists with the legacy WeatherPanel (WX
// sidebar entry) during development. Expected to replace it after Slices
// 5a-ii, 5a-iii, 5a-iv, 5b, 5c land.
//
// Slice 5a-i: shell only. Tab bodies are placeholders. Accept is a logging
// no-op + fake commit.
export default function WeatherPanelV2() {
  const activeTab = useWeatherV2Store(s => s.activeTab)

  let content
  switch (activeTab) {
    case 'global':     content = <GlobalTab />; break
    case 'patches':    content = <PatchesTab />; break
    case 'microburst': content = <MicroburstTab />; break
    default:           content = <GlobalTab />
  }

  return (
    <div>
      <WeatherV2Header />
      <WeatherV2Tabs />
      {content}
    </div>
  )
}
