import { useState, useMemo, useCallback, useRef, useEffect } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'

const STATE_GROUPS = [
  { key: 'simState',    label: 'sim_state',         defaultOpen: true,  scalar: true },
  { key: 'fdm',         label: 'fdm_state',         defaultOpen: true },
  { key: 'electrical',  label: 'electrical_state',   defaultOpen: false },
  { key: 'fuel',        label: 'fuel_state',         defaultOpen: false },
  { key: 'engines',     label: 'engine_state',       defaultOpen: false },
  { key: 'airData',     label: 'air_data_state',     defaultOpen: false },
  { key: 'nav',         label: 'nav_state',          defaultOpen: false },
  { key: 'gear',        label: 'gear_state',         defaultOpen: false },
  { key: 'arbitration', label: 'arbitration_state',  defaultOpen: false },
]

export default function InspectorPanel() {
  const state = useSimStore(useShallow(s => {
    const out = {}
    for (const g of STATE_GROUPS) out[g.key] = s[g.key]
    return out
  }))

  const [rawFilter, setRawFilter] = useState('')
  const [filter, setFilter] = useState('')
  const timerRef = useRef(null)

  const onFilterChange = useCallback((e) => {
    const v = e.target.value
    setRawFilter(v)
    clearTimeout(timerRef.current)
    timerRef.current = setTimeout(() => setFilter(v.toLowerCase()), 150)
  }, [])

  const clearFilter = useCallback(() => {
    setRawFilter('')
    setFilter('')
    clearTimeout(timerRef.current)
  }, [])

  useEffect(() => () => clearTimeout(timerRef.current), [])

  return (
    <div style={{ fontFamily: 'monospace', fontSize: 12 }}>
      {/* Search bar */}
      <div style={{ position: 'relative', marginBottom: 12 }}>
        <input
          value={rawFilter}
          onChange={onFilterChange}
          placeholder="Filter fields..."
          style={{
            width: '100%', boxSizing: 'border-box',
            padding: '8px 28px 8px 10px',
            background: '#111827', border: '1px solid #1e293b', borderRadius: 4,
            color: '#e2e8f0', fontFamily: 'monospace', fontSize: 12,
            outline: 'none',
          }}
        />
        {rawFilter && (
          <button onClick={clearFilter} style={{
            position: 'absolute', right: 6, top: '50%', transform: 'translateY(-50%)',
            background: 'none', border: 'none', color: '#64748b', cursor: 'pointer',
            fontSize: 14, padding: '0 4px', lineHeight: 1,
          }}>×</button>
        )}
      </div>

      {STATE_GROUPS.map((g, i) => (
        <div key={g.key}>
          {i > 0 && <div style={{ height: 1, background: '#1e293b', margin: '4px 0' }} />}
          <TreeRoot
            label={g.label}
            data={g.scalar ? { value: state[g.key] } : state[g.key]}
            defaultOpen={g.defaultOpen}
            filter={filter}
            scalar={g.scalar}
          />
        </div>
      ))}
    </div>
  )
}

function TreeRoot({ label, data, defaultOpen, filter, scalar }) {
  const [open, setOpen] = useState(defaultOpen)
  const forceOpen = filter.length > 0

  const keyCount = data && typeof data === 'object' ? Object.keys(data).length : 0
  const hasData = data != null && (typeof data !== 'object' || keyCount > 0)

  const matchesFilter = useMemo(() => {
    if (!filter) return true
    return hasData && matchesDeep(label, data, filter)
  }, [filter, label, data, hasData])

  if (filter && !matchesFilter) return null

  const isOpen = forceOpen || open

  return (
    <div>
      <div
        onClick={() => setOpen(o => !o)}
        style={{
          display: 'flex', alignItems: 'center', gap: 6, cursor: 'pointer',
          padding: '4px 0', userSelect: 'none',
        }}
      >
        <span style={{ color: '#64748b', fontSize: 10, width: 12, textAlign: 'center' }}>
          {isOpen ? '▼' : '▶'}
        </span>
        <span style={{ color: '#39d0d8', fontSize: 11, fontWeight: 700, textTransform: 'uppercase', letterSpacing: 1 }}>
          {label}
        </span>
        <span style={{ color: '#64748b', fontSize: 10 }}>
          {hasData ? (scalar ? '' : `(${keyCount})`) : '(no data)'}
        </span>
      </div>
      {isOpen && hasData && (
        <div style={{ paddingLeft: 12 }}>
          {scalar
            ? <ValueDisplay value={data.value} />
            : Object.entries(data).map(([k, v]) => (
                <TreeNode key={k} path={k} name={k} value={v} filter={filter} depth={0} />
              ))
          }
        </div>
      )}
    </div>
  )
}

function TreeNode({ path, name, value, filter, depth }) {
  const [open, setOpen] = useState(false)
  const forceOpen = filter.length > 0

  const isObject = value != null && typeof value === 'object' && !Array.isArray(value)
  const isNumericArray = Array.isArray(value) && value.length <= 8 && value.every(v => typeof v === 'number')
  const isArray = Array.isArray(value) && !isNumericArray

  const nodeMatches = useMemo(() => {
    if (!filter) return true
    return matchesDeep(path, value, filter)
  }, [filter, path, value])

  if (filter && !nodeMatches) return null

  const pad = depth * 16
  const isOpen = forceOpen || open

  // Leaf: primitive or short numeric array
  if (!isObject && !isArray) {
    return (
      <div style={{ display: 'flex', gap: 8, paddingLeft: pad, padding: '1px 0 1px ' + pad + 'px', alignItems: 'baseline' }}>
        <span style={{ color: '#94a3b8', flexShrink: 0 }}>{name}:</span>
        {isNumericArray
          ? <span style={{ color: '#e2e8f0' }}>[{value.map(formatNum).join(', ')}]</span>
          : <ValueDisplay value={value} />
        }
      </div>
    )
  }

  // Array of non-numbers or long array
  if (isArray) {
    return (
      <div style={{ paddingLeft: pad }}>
        <div onClick={() => setOpen(o => !o)} style={{ display: 'flex', gap: 6, cursor: 'pointer', padding: '1px 0', userSelect: 'none' }}>
          <span style={{ color: '#64748b', fontSize: 10, width: 12, textAlign: 'center' }}>{isOpen ? '▼' : '▶'}</span>
          <span style={{ color: '#94a3b8' }}>{name}</span>
          <span style={{ color: '#64748b', fontSize: 10 }}>[{value.length}]</span>
        </div>
        {isOpen && value.map((item, i) => (
          <TreeNode key={i} path={`${path}[${i}]`} name={`[${i}]`} value={item} filter={filter} depth={depth + 1} />
        ))}
      </div>
    )
  }

  // Object: collapsible group
  const entries = Object.entries(value)
  return (
    <div style={{ paddingLeft: pad }}>
      <div onClick={() => setOpen(o => !o)} style={{ display: 'flex', gap: 6, cursor: 'pointer', padding: '1px 0', userSelect: 'none' }}>
        <span style={{ color: '#64748b', fontSize: 10, width: 12, textAlign: 'center' }}>{isOpen ? '▼' : '▶'}</span>
        <span style={{ color: '#94a3b8' }}>{name}</span>
        <span style={{ color: '#64748b', fontSize: 10 }}>({entries.length})</span>
      </div>
      {isOpen && entries.map(([k, v]) => (
        <TreeNode key={k} path={`${path}.${k}`} name={k} value={v} filter={filter} depth={depth + 1} />
      ))}
    </div>
  )
}

function ValueDisplay({ value }) {
  if (value == null) return <span style={{ color: '#64748b' }}>(null)</span>
  if (typeof value === 'boolean') return <span style={{ color: value ? '#00ff88' : '#ff3b30' }}>{String(value)}</span>
  if (typeof value === 'number') return <span style={{ color: '#e2e8f0' }}>{formatNum(value)}</span>
  if (typeof value === 'string') return <span style={{ color: '#39d0d8' }}>"{value}"</span>
  return <span style={{ color: '#64748b' }}>{String(value)}</span>
}

function formatNum(n) {
  if (!Number.isFinite(n)) return String(n)
  const abs = Math.abs(n)
  if (abs >= 100) return n.toFixed(1)
  if (abs >= 1) return n.toFixed(2)
  if (abs >= 0.01) return n.toFixed(4)
  if (abs === 0) return '0'
  return n.toFixed(6)
}

function matchesDeep(path, value, filter) {
  if (path.toLowerCase().includes(filter)) return true
  if (value == null) return false
  if (typeof value !== 'object') return String(value).toLowerCase().includes(filter)
  if (Array.isArray(value)) return value.some((v, i) => matchesDeep(`${path}[${i}]`, v, filter))
  return Object.entries(value).some(([k, v]) => matchesDeep(`${path}.${k}`, v, filter))
}
