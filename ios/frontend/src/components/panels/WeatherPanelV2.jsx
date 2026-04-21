import { useEffect } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { useWeatherV2Store } from '../../store/useWeatherV2Store'
import WeatherV2Tabs     from './weatherV2/WeatherV2Tabs'
import GlobalTab         from './weatherV2/GlobalTab'
import PatchTab          from './weatherV2/PatchTab'
import MicroburstTab     from './weatherV2/MicroburstTab'
import PendingPatchState from './weatherV2/PendingPatchState'

// New weather authoring UI. Coexists with the legacy WeatherPanel (WX
// sidebar entry) during development. Expected to replace it after the
// 5a / 5b / 5c slices land.
//
// Slice 5b-iii: activeTab is 'global', 'microburst', or a patch's
// client_id. PatchTab is looked up by matching draft.patches client_id;
// if the lookup fails (e.g. the patch was just removed), we fall back
// to Global.
export default function WeatherPanelV2() {
  const activeTab     = useWeatherV2Store(s => s.activeTab)
  const patches       = useWeatherV2Store(s => s.draft.patches)
  const pendingTabs   = useWeatherV2Store(s => s.pendingTabs)
  const setActiveTab  = useWeatherV2Store(s => s.setActiveTab)
  const activeWeather = useSimStore(s => s.activeWeather)

  // Sync serverState (and clean draft) from the global weather broadcast.
  useEffect(() => {
    useWeatherV2Store.getState().syncFromBroadcast(activeWeather)
  }, [activeWeather])

  const activePatch = patches.find(p => p.client_id === activeTab)

  // Route: 'global' → GlobalTab. 'microburst' → MicroburstTab.
  // Role strings ('departure' / 'destination') with pendingTabs membership
  // → PendingPatchState. Anything else must be a patch client_id; if the
  // patch vanished (race during sync), fall back to Global.
  let content
  if (activeTab === 'global') {
    content = <GlobalTab />
  } else if (activeTab === 'microburst') {
    content = <MicroburstTab />
  } else if (pendingTabs.has(activeTab)) {
    content = <PendingPatchState role={activeTab} />
  } else if (activePatch) {
    content = <PatchTab patch={activePatch} />
  } else {
    // Stale tab id — recover silently.
    if (activeTab !== 'global') setActiveTab('global')
    content = <GlobalTab />
  }

  return (
    <div>
      <WeatherV2Tabs />
      {content}
    </div>
  )
}
