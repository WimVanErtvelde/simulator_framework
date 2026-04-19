import { useEffect, useRef, useState } from 'react'
import MSLAxis, { AXIS_WIDTH } from './graph/MSLAxis'
import CloudColumn from './CloudColumn'
import WindColumn from './WindColumn'

const MIN_GRAPH_HEIGHT   = 320
const BUTTON_ROW_SPACING = 44   // headroom for the + buttons above graph
const COLUMN_GAP_PX      = 12
const MIN_COL_WIDTH      = 120

// Middle column of WeatherPanelV2 Global tab. Hosts the MSL axis plus
// the cloud + wind layer columns side-by-side. Each column owns its
// own `+ …` button (anchored top: -36 inside its root), so they sit
// inside their respective column roots and don't collide horizontally.
export default function MSLGraphColumn() {
  const hostRef = useRef(null)
  const [size, setSize] = useState({ w: 600, h: 520 })

  useEffect(() => {
    if (!hostRef.current) return
    const ro = new ResizeObserver(entries => {
      for (const e of entries) {
        setSize({ w: e.contentRect.width, h: e.contentRect.height })
      }
    })
    ro.observe(hostRef.current)
    return () => ro.disconnect()
  }, [])

  const graphH  = Math.max(MIN_GRAPH_HEIGHT, size.h - BUTTON_ROW_SPACING)
  // Split the remaining width 50/50 between clouds and winds.
  const bodyW   = size.w - AXIS_WIDTH - COLUMN_GAP_PX
  const colW    = Math.max(MIN_COL_WIDTH, Math.floor(bodyW / 2))

  return (
    <div style={{
      padding: 12,
      background: '#0d1117',
      border: '1px solid #1e293b',
      borderRadius: 3,
      height: '100%', minHeight: 0,
      display: 'flex', flexDirection: 'column',
    }}>
      <div style={{
        fontSize: 11, fontWeight: 700, letterSpacing: 2,
        color: '#39d0d8', textTransform: 'uppercase',
        padding: '4px 0 6px', borderBottom: '1px solid #1e293b', marginBottom: 10,
      }}>Layer Graph</div>

      {/* Headroom for the absolutely-positioned + buttons at top of each column */}
      <div style={{ height: 32 }} />

      <div ref={hostRef} style={{
        position: 'relative',
        flex: 1,
        display: 'flex',
        alignItems: 'stretch',
        gap: 0,
      }}>
        <MSLAxis height={graphH} />
        <CloudColumn height={graphH} width={colW} />
        <WindColumn  height={graphH} width={colW} />
      </div>
    </div>
  )
}
