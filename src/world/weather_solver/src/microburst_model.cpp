#include "weather_solver/microburst_model.hpp"

#include <cmath>
#include <algorithm>

namespace weather_solver
{

// WGS-84 semi-major axis
static constexpr double EARTH_A = 6378137.0;
static constexpr double DEG_TO_RAD = M_PI / 180.0;

// Lifecycle timing
static constexpr float RAMP_UP_SEC   = 60.0f;   // Intensifying: 0→1 over 60s
static constexpr float RAMP_DOWN_SEC = 120.0f;   // Decaying: 1→0 over 120s

/// Compute effective intensity scale factor based on lifecycle phase.
static double lifecycle_factor(uint8_t phase, float activation_time_sec,
                               double sim_time_sec)
{
    switch (phase) {
    case 0: return 0.0;  // Dormant
    case 1: {  // Intensifying
        double elapsed = sim_time_sec - static_cast<double>(activation_time_sec);
        if (elapsed <= 0.0) return 0.0;
        return std::clamp(elapsed / RAMP_UP_SEC, 0.0, 1.0);
    }
    case 2: return 1.0;  // Mature
    case 3: {  // Decaying
        double elapsed = sim_time_sec - static_cast<double>(activation_time_sec);
        if (elapsed <= 0.0) return 1.0;
        return std::clamp(1.0 - elapsed / RAMP_DOWN_SEC, 0.0, 1.0);
    }
    default: return 0.0;
    }
}

/// Sample a single microburst hazard at the aircraft position.
static MicroburstModel::WindContribution sample_single(
    const MicroburstModel::HazardParams & h,
    double ac_north, double ac_east,
    double alt_agl, double sim_time)
{
    MicroburstModel::WindContribution w{};

    double lf = lifecycle_factor(h.lifecycle_phase, h.activation_time_sec, sim_time);
    if (lf <= 0.0) return w;

    double lambda = static_cast<double>(h.intensity) * lf;
    double R = std::max(static_cast<double>(h.core_radius_m), 10.0);
    double z_m = std::max(static_cast<double>(h.shaft_altitude_m), 10.0);

    // Relative position
    double dn = ac_north - h.north_m;
    double de = ac_east  - h.east_m;
    double r  = std::sqrt(dn * dn + de * de);
    double r_eff = std::max(r, 10.0);  // avoid singularity
    double z  = std::max(alt_agl, 0.1);

    // Radial outflow profile
    double r_ratio = r_eff / R;
    double V_r = lambda * (R / r_eff) * (1.0 - std::exp(-r_ratio * r_ratio));

    // Vertical profile factor for radial wind
    double z_norm = (z - z_m) / (z_m * 0.4);
    double f_z = std::exp(-0.5 * z_norm * z_norm);
    V_r *= f_z;

    // Downdraft profile (strongest at center, decays with distance)
    double V_z = -lambda * std::exp(-(r / R) * (r / R));

    // Ground taper for downdraft
    double g_z;
    double z_taper = z_m * 0.5;
    if (z < z_taper) {
        g_z = z / z_taper;
    } else {
        g_z = 1.0;
    }
    V_z *= g_z;

    // Convert to NED
    if (r > 0.1) {
        double bearing_cos = dn / r;
        double bearing_sin = de / r;
        w.north_ms = V_r * bearing_cos;
        w.east_ms  = V_r * bearing_sin;
    }
    w.down_ms = -V_z;  // V_z negative = downdraft, wind_down positive = downdraft

    return w;
}

MicroburstModel::WindContribution MicroburstModel::sample(
    const std::vector<HazardParams> & hazards,
    double aircraft_north_m,
    double aircraft_east_m,
    double altitude_agl_m,
    double sim_time_sec)
{
    WindContribution total{};
    for (const auto & h : hazards) {
        auto c = sample_single(h, aircraft_north_m, aircraft_east_m,
                               altitude_agl_m, sim_time_sec);
        total.north_ms += c.north_ms;
        total.east_ms  += c.east_ms;
        total.down_ms  += c.down_ms;
    }
    return total;
}

void MicroburstModel::latlon_to_ne(
    double ref_lat_deg, double ref_lon_deg,
    double pt_lat_deg, double pt_lon_deg,
    double & north_m, double & east_m)
{
    // Flat-earth approximation
    double ref_lat_rad = ref_lat_deg * DEG_TO_RAD;
    north_m = (pt_lat_deg - ref_lat_deg) * DEG_TO_RAD * EARTH_A;
    east_m  = (pt_lon_deg - ref_lon_deg) * DEG_TO_RAD * EARTH_A * std::cos(ref_lat_rad);
}

}  // namespace weather_solver
