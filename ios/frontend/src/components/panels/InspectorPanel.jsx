import { useState, useMemo, useCallback, useRef, useEffect } from 'react'
import { useSimStore } from '../../store/useSimStore'
import { useShallow } from 'zustand/react/shallow'

// Fixed root order
const ROOTS = [
  { prefix: '/world',    label: '/world' },
  { prefix: '/aircraft', label: '/aircraft' },
  { prefix: '/sim',      label: '/sim' },
  { prefix: '/clock',    label: '/clock' },
]

// Topics expanded by default
const DEFAULT_OPEN = new Set([
  '/aircraft/fdm/state',
  '/sim/state',
])

export default function InspectorPanel() {
  const { topicTree, topicValues } = useSimStore(useShallow(s => ({
    topicTree: s.topicTree,
    topicValues: s.topicValues,
  })))

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

  // Build nested tree from flat topic paths
  const tree = useMemo(() => buildTree(topicTree, topicValues), [topicTree, topicValues])

  const isEmpty = Object.keys(topicTree).length === 0

  return (
    <div style={{ fontFamily: 'monospace', fontSize: 12 }}>
      {/* Search bar */}
      <div style={{ position: 'relative', marginBottom: 12 }}>
        <input
          value={rawFilter}
          onChange={onFilterChange}
          placeholder="Filter topics and fields..."
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

      {isEmpty && (
        <div style={{ color: '#64748b', textAlign: 'center', padding: '32px 0', animation: 'pulse 2s ease-in-out infinite' }}>
          Waiting for topic discovery...
          <style>{`@keyframes pulse { 0%,100% { opacity: 0.5 } 50% { opacity: 1 } }`}</style>
        </div>
      )}

      {!isEmpty && ROOTS.map((root, i) => {
        const subtree = tree[root.prefix]
        if (!subtree) return null
        return (
          <div key={root.prefix}>
            {i > 0 && <div style={{ height: 1, background: '#1e293b', margin: '4px 0' }} />}
            <NamespaceNode
              segment={root.label}
              fullPath={root.prefix}
              node={subtree}
              depth={0}
              filter={filter}
              defaultOpen={true}
            />
          </div>
        )
      })}
    </div>
  )
}

// ─── Tree Builder ───────────────────────────────────────────────────

function buildTree(topicTree, topicValues) {
  const root = {}
  for (const [topicPath, meta] of Object.entries(topicTree)) {
    // Split "/aircraft/fdm/state" → ["", "aircraft", "fdm", "state"]
    const parts = topicPath.split('/')
    let cursor = root

    // Build path one segment at a time, using "/" prefix keys
    for (let i = 1; i < parts.length; i++) {
      const key = '/' + parts[i]
      // Last segment → topic leaf
      if (i === parts.length - 1) {
        cursor[key] = {
          _meta: meta,
          _data: topicValues[topicPath] ?? null,
          _path: topicPath,
        }
      } else {
        if (!cursor[key] || cursor[key]._meta) {
          // Don't overwrite a leaf with a namespace
          if (!cursor[key] || cursor[key]._meta) cursor[key] = cursor[key] ? { ...cursor[key] } : {}
        }
        cursor = cursor[key]
      }
    }
  }
  return root
}

// ─── Namespace Node (recursive) ─────────────────────────────────────

function NamespaceNode({ segment, fullPath, node, depth, filter, defaultOpen }) {
  const [open, setOpen] = useState(defaultOpen ?? false)
  const forceOpen = filter.length > 0

  // If this node has _meta, it's a topic leaf
  if (node._meta) {
    return <TopicLeaf
      segment={segment}
      fullPath={node._path || fullPath}
      meta={node._meta}
      data={node._data}
      depth={depth}
      filter={filter}
    />
  }

  // Namespace group — collect children
  const children = Object.entries(node).filter(([k]) => k.startsWith('/'))
  const topicCount = countTopics(node)

  const matchesFilter = useMemo(() => {
    if (!filter) return true
    if (fullPath.toLowerCase().includes(filter)) return true
    return children.some(([k, v]) => nodeMatchesFilter(fullPath + k, v, filter))
  }, [filter, fullPath, children])

  if (filter && !matchesFilter) return null

  const isOpen = forceOpen || open

  return (
    <div style={{ paddingLeft: depth > 0 ? 16 : 0 }}>
      <div
        onClick={() => setOpen(o => !o)}
        style={{
          display: 'flex', alignItems: 'center', gap: 6,
          cursor: 'pointer', padding: '3px 0', userSelect: 'none',
        }}
      >
        <span style={{ color: '#64748b', fontSize: 10, width: 12, textAlign: 'center' }}>
          {isOpen ? '▼' : '▶'}
        </span>
        <span style={{ color: '#39d0d8', fontSize: 11, fontWeight: 700 }}>
          {segment}
        </span>
        <span style={{ color: '#64748b', fontSize: 10 }}>
          ({topicCount})
        </span>
      </div>
      {isOpen && children
        .sort(([a], [b]) => a.localeCompare(b))
        .map(([key, child]) => (
          <NamespaceNode
            key={key}
            segment={key}
            fullPath={fullPath + key}
            node={child}
            depth={depth + 1}
            filter={filter}
            defaultOpen={DEFAULT_OPEN.has((child._path || fullPath + key))}
          />
        ))
      }
    </div>
  )
}

function countTopics(node) {
  if (node._meta) return 1
  let count = 0
  for (const [k, v] of Object.entries(node)) {
    if (k.startsWith('/')) count += countTopics(v)
  }
  return count
}

function nodeMatchesFilter(path, node, filter) {
  if (path.toLowerCase().includes(filter)) return true
  if (node._meta) {
    if (node._meta.type?.toLowerCase().includes(filter)) return true
    if (node._data) return matchesDeep(path, node._data, filter)
    return false
  }
  return Object.entries(node).some(([k, v]) => k.startsWith('/') && nodeMatchesFilter(path + k, v, filter))
}

// ─── Topic Leaf ─────────────────────────────────────────────────────

function TopicLeaf({ segment, fullPath, meta, data, depth, filter }) {
  const [open, setOpen] = useState(DEFAULT_OPEN.has(fullPath))
  const forceOpen = filter.length > 0

  const typeName = meta.type?.split('/').pop() || meta.type || ''

  const matchesFilter = useMemo(() => {
    if (!filter) return true
    if (fullPath.toLowerCase().includes(filter)) return true
    if (typeName.toLowerCase().includes(filter)) return true
    if (data) return matchesDeep(fullPath, data, filter)
    return false
  }, [filter, fullPath, typeName, data])

  if (filter && !matchesFilter) return null

  const isOpen = forceOpen || open

  return (
    <div style={{ paddingLeft: depth > 0 ? 16 : 0 }}>
      <div
        onClick={() => setOpen(o => !o)}
        style={{
          display: 'flex', alignItems: 'center', gap: 8,
          cursor: 'pointer', padding: '3px 0', userSelect: 'none',
        }}
      >
        <span style={{ color: '#64748b', fontSize: 10, width: 12, textAlign: 'center' }}>
          {isOpen ? '▼' : '▶'}
        </span>
        <span style={{ color: '#e2e8f0', fontSize: 12 }}>
          {segment}
        </span>
        <span style={{ color: '#475569', fontSize: 10 }}>
          {typeName}
        </span>
        <span style={{
          width: 6, height: 6, borderRadius: '50%', flexShrink: 0,
          background: meta.has_data ? '#00ff88' : '#334155',
        }} />
      </div>
      {isOpen && (
        <div style={{ paddingLeft: 20 }}>
          {data
            ? Object.entries(data).map(([k, v]) => (
                <TreeNode key={k} path={`${fullPath}.${k}`} name={k} value={v} filter={filter} depth={0} />
              ))
            : <span style={{ color: '#64748b', fontStyle: 'italic' }}>(awaiting first message)</span>
          }
        </div>
      )}
    </div>
  )
}

// ─── Recursive Value Renderer (reused from Phase 1) ─────────────────

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

  if (!isObject && !isArray) {
    return (
      <div style={{ display: 'flex', gap: 8, padding: `1px 0 1px ${pad}px`, alignItems: 'baseline' }}>
        <span style={{ color: '#94a3b8', flexShrink: 0 }}>{name}:</span>
        {isNumericArray
          ? <span style={{ color: '#e2e8f0' }}>[{value.map(formatNum).join(', ')}]</span>
          : <ValueDisplay value={value} />
        }
      </div>
    )
  }

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
