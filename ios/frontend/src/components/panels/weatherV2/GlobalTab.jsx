import { useEffect, useState } from 'react'
import LayerPropertiesColumn from './LayerPropertiesColumn'
import MSLGraphColumn        from './MSLGraphColumn'
import GlobalScalarsPanel    from './GlobalScalarsPanel'

// 3-column layout modeled on X-Plane 12's Weather Settings screen:
//   Left   — Layer Properties (populated in 5a-iii/iv)
//   Middle — MSL Graph with + Cloud/Wind buttons (populated in 5a-iii/iv)
//   Right  — Atmospheric + Runway conditions
//
// Below 1200 px viewport width, columns stack vertically so each still has
// usable width. Threshold is set on window.innerWidth (not panel width)
// because it correlates closely enough and avoids a ResizeObserver.

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

export default function GlobalTab() {
  const wide = useIsWideViewport(WIDE_BREAKPOINT)

  return (
    <div style={{
      display: 'grid',
      gridTemplateColumns: wide
        ? 'minmax(260px, 22%) minmax(320px, 46%) minmax(300px, 32%)'
        : '1fr',
      gap: 12,
      minHeight: 600,
    }}>
      <LayerPropertiesColumn />
      <MSLGraphColumn />
      <GlobalScalarsPanel />
    </div>
  )
}
