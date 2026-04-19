import { useEffect } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { useWeatherV2Store } from '../../store/useWeatherV2Store'
import WeatherV2Tabs from './weatherV2/WeatherV2Tabs'
import GlobalTab     from './weatherV2/GlobalTab'
import PatchesTab    from './weatherV2/PatchesTab'
import MicroburstTab from './weatherV2/MicroburstTab'

// New weather authoring UI. Coexists with the legacy WeatherPanel (WX
// sidebar entry) during development. Expected to replace it after Slices
// 5a-iii / 5a-iv / 5b / 5c land.
//
// Slice 5a-ii.2: single row combining tabs + Accept/Discard; no separate
// header strip.
export default function WeatherPanelV2() {
  const activeTab     = useWeatherV2Store(s => s.activeTab)
  const activeWeather = useSimStore(s => s.activeWeather)

  // Sync serverState (and clean draft) from the global weather broadcast.
  useEffect(() => {
    useWeatherV2Store.getState().syncFromBroadcast(activeWeather)
  }, [activeWeather])

  let content
  switch (activeTab) {
    case 'global':     content = <GlobalTab />; break
    case 'patches':    content = <PatchesTab />; break
    case 'microburst': content = <MicroburstTab />; break
    default:           content = <GlobalTab />
  }

  return (
    <div>
      <WeatherV2Tabs />
      {content}
    </div>
  )
}
