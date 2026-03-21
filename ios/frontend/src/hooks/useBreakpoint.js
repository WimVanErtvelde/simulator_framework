import { useState, useEffect } from 'react'

export function useBreakpoint() {
  const [bp, setBp] = useState(window.innerWidth >= 1400 ? 'desktop' : 'tablet')
  useEffect(() => {
    const handler = () => setBp(window.innerWidth >= 1400 ? 'desktop' : 'tablet')
    window.addEventListener('resize', handler)
    return () => window.removeEventListener('resize', handler)
  }, [])
  return bp
}
