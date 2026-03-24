---
name: CIGI IG repositioning handshake
description: Future feature — X-Plane plugin should detect large position changes, show repositioning dialog, wait for terrain paging, then signal ready to host
type: project
---

X-Plane CIGI plugin should detect IC position changes and signal terrain readiness.

**Why:** When repositioning to a distant airport, X-Plane needs time to page terrain tiles. Currently the sim sends HOT requests immediately and uses SRTM as fallback if CIGI doesn't respond within 2s. A proper handshake would ensure IG terrain is loaded before the sim relies on it.

**How to apply:** When implementing Entity Control (0x03) handling in the X-Plane plugin, add position change detection (>1nm threshold), terrain stabilization polling via XPLMProbeTerrainXYZ, and a response packet to the host signaling readiness. The host (cigi_bridge) would wait for this signal before trusting HOT data at the new position.
