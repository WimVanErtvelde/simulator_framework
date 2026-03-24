---
name: Terrain service must be sim-side
description: Terrain elevation is sim-side infrastructure (SRTM/DTED) — sim must work without IG connected, IG does not provide LOS calculations
type: project
---

Terrain service stays as sim-side infrastructure, NOT dependent on IG.

**Why:** The sim must be testable without an IG connected. The IG (e.g. X-Plane) does not provide a line-of-sight calculation interface — LOS for VHF navaid signal propagation is always computed sim-side from DTED/SRTM data.

**How to apply:** When the terrain service decision comes up (separate node vs merged into navaid_sim), both options keep terrain sim-side. Never design terrain queries that require an IG connection to function. CIGI HOT responses from the IG are supplementary (gear contact precision), not the primary terrain source.
