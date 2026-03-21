#include "LOSChecker.h"
#include "Units.h"    // ft_to_m(), nm_to_m(), rad_to_deg(), deg_to_rad()

#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
LOSChecker::LOSChecker(TerrainModel* terrain)
    : mTerrain(terrain)
{
}

// ---------------------------------------------------------------------------
// radioHorizonNM
// ---------------------------------------------------------------------------
// Classic radio-horizon formula for VHF/UHF propagation over a 4/3 earth:
//   d [NM] = 1.23 * sqrt(h [ft])   for a single endpoint.
// The combined horizon for two elevated ends is the sum.
// ---------------------------------------------------------------------------
float LOSChecker::radioHorizonNM(float aircraft_alt_ft, float navaid_elev_ft)
{
    float d_aircraft = 1.23f * std::sqrt(std::max(0.0f, aircraft_alt_ft));
    float d_navaid   = 1.23f * std::sqrt(std::max(0.0f, navaid_elev_ft));
    return d_aircraft + d_navaid;
}

// ---------------------------------------------------------------------------
// slerp — spherical linear interpolation of two lat/lon positions
// ---------------------------------------------------------------------------
// Uses the standard SLERP formula on the unit sphere.
// For NavSim ranges (< 300 NM) a simple linear interpolation of lat/lon
// would be adequate, but SLERP is correct for any distance.
// ---------------------------------------------------------------------------
LatLon LOSChecker::slerp(const LatLon& p1, const LatLon& p2, float t)
{
    double lat1 = p1.get_lat_rad();
    double lon1 = p1.get_lon_rad();
    double lat2 = p2.get_lat_rad();
    double lon2 = p2.get_lon_rad();

    // Convert to unit Cartesian vectors
    double x1 = std::cos(lat1) * std::cos(lon1);
    double y1 = std::cos(lat1) * std::sin(lon1);
    double z1 = std::sin(lat1);

    double x2 = std::cos(lat2) * std::cos(lon2);
    double y2 = std::cos(lat2) * std::sin(lon2);
    double z2 = std::sin(lat2);

    // Angular distance between the two points
    double dot = std::max(-1.0, std::min(1.0, x1*x2 + y1*y2 + z1*z2));
    double omega = std::acos(dot);

    if (omega < 1e-10)
        return p1;   // points are essentially coincident

    double sinOmega = std::sin(omega);
    double A = std::sin((1.0 - t) * omega) / sinOmega;
    double B = std::sin(       t  * omega) / sinOmega;

    double x = A * x1 + B * x2;
    double y = A * y1 + B * y2;
    double z = A * z1 + B * z2;

    double lat = std::atan2(z, std::sqrt(x*x + y*y));
    double lon = std::atan2(y, x);

    return LatLon(rad_to_deg(lat), rad_to_deg(lon));
}

// ---------------------------------------------------------------------------
// checkTerrainPath
// ---------------------------------------------------------------------------
// Walks the great-circle path from p1 (height h1_m) to p2 (height h2_m).
// At each sample, the effective terrain height is terrain_elev + earth_bulge.
// If that exceeds the straight-line LOS height at that fraction → blocked.
//
// Earth-bulge formula (4/3 model, all units metres):
//   bulge(d) = d * (D - d) / (2 * RE_EFF_M)
// where d = distance from p1 to sample, D = total path length.
// ---------------------------------------------------------------------------
bool LOSChecker::checkTerrainPath(const LatLon& p1, float h1_m,
                                  const LatLon& p2, float h2_m,
                                  float dist_nm) const
{
    float D_m = (float)nm_to_m(dist_nm);

    // Number of interior samples (exclude endpoints — they're the antennas)
    int nSamples = std::max(MIN_SAMPLES,
                            (int)(dist_nm / SAMPLE_STEP_NM));

    for (int i = 1; i < nSamples; ++i)
    {
        float t = (float)i / (float)nSamples;   // 0 < t < 1

        // Position of this sample on the great-circle path
        LatLon sample = slerp(p1, p2, t);

        // Terrain elevation at this position (metres MSL)
        float terrain_m = mTerrain->getElevationM(
            sample.get_lat_deg(), sample.get_lon_deg());

        // Earth-bulge correction (metres): raises effective terrain height
        float d_m    = t * D_m;
        float bulge  = (float)(d_m * (D_m - d_m) / (2.0 * RE_EFF_M));

        // Straight-line LOS height at this fraction (metres MSL)
        float los_m = h1_m + t * (h2_m - h1_m);

        // Obstruction check: effective terrain > LOS height → blocked
        if ((terrain_m + bulge) > los_m)
            return false;
    }

    return true;   // no obstruction found
}

// ---------------------------------------------------------------------------
// hasLOS — main entry point
// ---------------------------------------------------------------------------
bool LOSChecker::hasLOS(const LatLon& aircraft_pos, float aircraft_alt_ft,
                        const LatLon& navaid_pos,   float navaid_elev_ft) const
{
    // If no terrain data, assume clear LOS (graceful degradation)
    if (!mTerrain || !mTerrain->isAvailable())
        return true;

    // Ground distance (horizontal) between aircraft and navaid
    float dist_nm = aircraft_pos.get_distance_nm(navaid_pos);

    // --- Radio horizon pre-check (fast reject) ---
    float horizon_nm = radioHorizonNM(aircraft_alt_ft, navaid_elev_ft);
    if (dist_nm > horizon_nm)
        return false;

    // --- Terrain path check ---
    float h_aircraft_m = (float)ft_to_m(aircraft_alt_ft);
    float h_navaid_m   = (float)ft_to_m(navaid_elev_ft);

    return checkTerrainPath(aircraft_pos, h_aircraft_m,
                            navaid_pos,   h_navaid_m,
                            dist_nm);
}
