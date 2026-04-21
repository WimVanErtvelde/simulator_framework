import { useEffect, useState } from 'react'
import LayerPropertiesColumn   from './LayerPropertiesColumn'
import MSLGraphColumn          from './MSLGraphColumn'
import PatchScalarsPanel       from './PatchScalarsPanel'
import PatchHeader             from './PatchHeader'
import EmptyPatchState         from './EmptyPatchState'
import PatchMicroburstSection  from './PatchMicroburstSection'

const WIDE_BREAKPOINT = 1200

function useIsWideViewport(threshold) {
  const [wide, setWide] = useState(() =>
    typeof window !== 'undefined' && window.innerWidth >= threshold
  )
  useEffect(() => {
    const onResize = () => setWide(window.innerWidth >= threshold)
    window.addEventListener('resize', onResize)
    return () => window.removeEventListener('resize', onResize)
  }, [threshold])
  return wide
}

// Patch tab composition — header on top, then the same 3-column layout
// as the Global tab (LayerProperties / MSLGraph / Scalars). patchContext
// retargets the middle and left columns to read/write the patch's
// cloud_layers / wind_layers instead of global's. Scalars panel is a
// different file (PatchScalarsPanel) since every field gets an override
// toggle.
//
// When the patch has no airport yet, show EmptyPatchState instead of
// the full UI — prevents authoring against a (0, 0, 0) location.
export default function PatchTab({ patch }) {
  const wide = useIsWideViewport(WIDE_BREAKPOINT)

  if (!patch.icao) {
    return <EmptyPatchState patch={patch} />
  }

  const patchContext = {
    client_id:    patch.client_id,
    stationElevM: patch.ground_elevation_m,
    stationIcao:  patch.icao,
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
      <PatchHeader patch={patch} />

      <div style={{
        display: 'grid',
        gridTemplateColumns: wide
          ? 'minmax(260px, 22%) minmax(320px, 46%) minmax(300px, 32%)'
          : '1fr',
        gap: 12,
        minHeight: 600,
      }}>
        <LayerPropertiesColumn patchContext={patchContext} />
        <MSLGraphColumn         patchContext={patchContext} />
        <div style={{ display: 'flex', flexDirection: 'column', gap: 12, minHeight: 0 }}>
          <PatchScalarsPanel       patch={patch} />
          <PatchMicroburstSection  patch={patch} />
        </div>
      </div>
    </div>
  )
}
