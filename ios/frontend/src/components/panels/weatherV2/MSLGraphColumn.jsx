import { useEffect, useRef, useState } from 'react'
import MSLAxis, { AXIS_WIDTH } from './graph/MSLAxis'
import CloudColumn from './CloudColumn'

const MIN_GRAPH_HEIGHT   = 320
const BUTTON_ROW_SPACING = 44   // space reserved above graph for + Cloud Layer btn

// Middle column of WeatherPanelV2 Global tab. Hosts the MSL axis and the
// cloud-layer graph. Wind column is added alongside CloudColumn in 5a-iv.
export default function MSLGraphColumn() {
  const hostRef = useRef(null)
  const [size, setSize] = useState({ w: 400, h: 520 })

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

  const graphH = Math.max(MIN_GRAPH_HEIGHT, size.h - BUTTON_ROW_SPACING)
  const cloudColW = Math.max(120, size.w - AXIS_WIDTH - 12)

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

      {/* Spacer so the absolutely-positioned "+ Cloud Layer" button has
          room at the top of the graph body. */}
      <div style={{ height: 32 }} />

      <div ref={hostRef} style={{
        position: 'relative',
        flex: 1,
        display: 'flex',
        alignItems: 'stretch',
        gap: 0,
      }}>
        <MSLAxis height={graphH} />
        <CloudColumn height={graphH} width={cloudColW} />
      </div>
    </div>
  )
}
