import GlobalScalarsPanel from './GlobalScalarsPanel'

export default function GlobalTab() {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
      <GlobalScalarsPanel />
      <div style={{
        fontSize: 10, color: '#475569', fontFamily: 'monospace', fontStyle: 'italic',
        padding: '8px 0', borderTop: '1px solid #1e293b',
      }}>
        Cloud layers (Slice 5a-iii) and wind layers (Slice 5a-iv) will appear below.
      </div>
    </div>
  )
}
