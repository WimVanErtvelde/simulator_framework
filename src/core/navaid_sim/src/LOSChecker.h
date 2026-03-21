#pragma once
#ifndef LOS_CHECKER_H
#define LOS_CHECKER_H

#include "TerrainModel.h"
#include "LatLon.h"

// ---------------------------------------------------------------------------
// LOSChecker
// ---------------------------------------------------------------------------
// Determines terrain line-of-sight (LOS) between an aircraft and a navaid.
//
// Physics model:
//   - 4/3 earth radius (effective radius Re_eff = 4/3 * 6371 km) to account
//     for standard atmospheric refraction of VHF/UHF radio signals.
//   - Earth-bulge correction: at distance d along a total path D (metres),
//       bulge(d) = d * (D - d) / (2 * Re_eff)    [metres]
//     The terrain at that point is effectively raised by bulge(d) relative
//     to the straight-line view between transmitter and receiver.
//   - Terrain is sampled at ~0.5 NM intervals along the great-circle path.
//
// Radio horizon pre-check (fast reject):
//   d_max [NM] = 1.23 * ( sqrt(h_aircraft_ft) + sqrt(h_navaid_ft) )
//   If slant range > d_max the signal is beyond the horizon → no LOS.
//
// LOS is only meaningful for VHF/UHF navaids (VOR, DME, ILS LOC/GS).
// NDB operates on LF/MF ground-wave propagation and should bypass this check.
// ---------------------------------------------------------------------------
class LOSChecker
{
public:
    // terrain: pointer to a TerrainModel.  May be nullptr — hasLOS() will
    //          then return true (no terrain data available, assume clear).
    explicit LOSChecker(TerrainModel* terrain);

    // Returns true if there is unobstructed LOS between the aircraft and the
    // navaid antenna.
    //
    //  aircraft_pos    : aircraft position
    //  aircraft_alt_ft : aircraft altitude MSL, feet
    //  navaid_pos      : navaid antenna position
    //  navaid_elev_ft  : navaid antenna elevation MSL, feet
    bool hasLOS(const LatLon& aircraft_pos, float aircraft_alt_ft,
                const LatLon& navaid_pos,   float navaid_elev_ft) const;

    // Radio horizon range in NM for the given pair of heights.
    // Accounts for both aircraft and navaid antenna heights.
    static float radioHorizonNM(float aircraft_alt_ft, float navaid_elev_ft);

private:
    TerrainModel* mTerrain;

    // Walk the great-circle path and check for terrain obstructions.
    // h1_m, h2_m : heights of the endpoints above MSL, in metres.
    // dist_nm    : pre-computed horizontal ground distance, nautical miles.
    bool checkTerrainPath(const LatLon& p1, float h1_m,
                          const LatLon& p2, float h2_m,
                          float dist_nm) const;

    // Spherical linear interpolation (SLERP) between two lat/lon points.
    // t = 0 → p1,  t = 1 → p2.
    static LatLon slerp(const LatLon& p1, const LatLon& p2, float t);

    // Effective earth radius for the 4/3 model, metres.
    static constexpr double RE_EFF_M = (4.0 / 3.0) * 6371000.0;

    // Sampling interval along the path, nautical miles.
    static constexpr float SAMPLE_STEP_NM = 0.5f;

    // Minimum samples for very short paths.
    static constexpr int MIN_SAMPLES = 5;
};

#endif // LOS_CHECKER_H
