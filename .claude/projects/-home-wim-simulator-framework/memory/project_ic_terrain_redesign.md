---
name: IC terrain redesign — 0 MSL then CIGI reposition
description: Planned IC flow simplification — position at 0 MSL first, wait for CIGI HOT, then reposition to correct height
type: project
---

Current IC terrain flow is complex (raw IC → SRTM async → CIGI HOT refine, 3 stages). Planned simplification:

1. Set aircraft at target lat/lon, altitude = 0 MSL (temporary)
2. CIGI bridge sends HOT requests at new position (terrain gets paged by IG)
3. Wait for HOT responses (3 gear contact points)
4. Compute correct altitude from terrain + gear geometry
5. Re-apply IC with correct altitude — single clean reposition

**Why:** Eliminates SRTM dependency for on-ground starts when IG is connected. Ensures terrain is paged at target position before probing. Avoids underground/floating issues from mismatched terrain sources. Falls back to SRTM only when no IG connected.

**How to apply:** Modify flight_model_adapter IC handler. May need 3-point ground plane computation from gear contact HOTs for sloped terrain. The X-Plane plugin's XPLMProbeTerrainXYZ works at any lat/lon where terrain is loaded — moving the entity there first guarantees terrain paging.

**Status:** Not yet implemented. Current code uses 3-stage approach (raw → SRTM → CIGI refine). The JSBSim terrain-elevation-asl-ft property is now set before IC (fixed in this session).
