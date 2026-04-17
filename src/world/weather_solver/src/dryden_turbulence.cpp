#include "weather_solver/dryden_turbulence.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>

namespace weather_solver
{

// ─── Constants ──────────────────────────────────────────────────────────────
static constexpr double M_TO_FT = 3.28084;
static constexpr double FT_TO_M = 1.0 / M_TO_FT;

// MIL-F-8785C medium/high altitude (h > 2000 ft AGL)
static constexpr double HI_SCALE_FT = 1750.0;   // L_u = L_v = L_w
static constexpr double HI_SCALE_M  = HI_SCALE_FT * FT_TO_M;

// Altitude bands (metres AGL)
static constexpr double LOW_ALT_M  = 304.8;   // 1000 ft
static constexpr double HIGH_ALT_M = 609.6;   // 2000 ft
static constexpr double MIN_ALT_M  = 3.048;   // 10 ft clamp

// Maximum sigma at severity=1 for medium/high altitude
static constexpr double SIGMA_MAX_MS = 3.0;

// ─── Helpers ────────────────────────────────────────────────────────────────

/// Compute MIL-F-8785C low-altitude scale lengths and sigmas.
/// h_ft: altitude AGL in feet (clamped to >= 10 ft)
/// W_20: wind speed at 20 ft reference height (m/s), derived from severity
static void low_alt_params(double h_ft, double W_20,
                           double & L_u_m, double & L_v_m, double & L_w_m,
                           double & sigma_u, double & sigma_v, double & sigma_w)
{
    h_ft = std::max(h_ft, 10.0);

    // Scale lengths (MIL-F-8785C Table I)
    L_u_m = (h_ft / std::pow(0.177 + 0.000823 * h_ft, 1.2)) * FT_TO_M;
    L_v_m = L_u_m * 0.5;
    L_w_m = h_ft * FT_TO_M;

    // Intensities
    sigma_w = 0.1 * W_20;
    sigma_u = sigma_w / std::pow(0.177 + 0.000823 * h_ft, 0.4);
    sigma_v = sigma_u;
}

/// First-order discrete filter: y[n] = a * y[n-1] + b * noise
static double filter1(double state, double dt, double V, double L, double sigma,
                      double noise)
{
    if (L <= 0.0 || V <= 0.0) return 0.0;
    double ratio = V * dt / L;
    double a = 1.0 - ratio;
    a = std::clamp(a, -0.99, 0.99);  // stability guard
    double b = sigma * std::sqrt(2.0 * ratio);
    return a * state + b * noise;
}

// ─── Public ─────────────────────────────────────────────────────────────────

void DrydenTurbulence::configure(const Config & cfg)
{
    model_ = cfg.model;
    if (cfg.seed != 0) {
        rng_.seed(cfg.seed);
    } else {
        rng_.seed(static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    }
    reset();
}

void DrydenTurbulence::reset()
{
    state_u_  = 0.0;
    state_v1_ = 0.0;
    state_v2_ = 0.0;
    state_w1_ = 0.0;
    state_w2_ = 0.0;
}

DrydenTurbulence::Perturbation DrydenTurbulence::update(
    double dt_sec, double severity,
    double altitude_agl_m, double tas_ms)
{
    Perturbation p{};
    if (model_ == Model::NONE || severity <= 0.0 || dt_sec <= 0.0) return p;
    if (tas_ms < 1.0) tas_ms = 1.0;  // avoid division by zero

    severity = std::clamp(severity, 0.0, 1.0);
    altitude_agl_m = std::max(altitude_agl_m, MIN_ALT_M);

    // Generate white noise samples
    std::normal_distribution<double> norm(0.0, 1.0);
    double n_u = norm(rng_);
    double n_v = norm(rng_);
    double n_w = norm(rng_);

    double L_u, L_v, L_w, sigma_u, sigma_v, sigma_w;

    double h_ft = altitude_agl_m * M_TO_FT;

    if (altitude_agl_m <= LOW_ALT_M) {
        // Low altitude: MIL-F-8785C Table I
        double W_20 = severity * 30.0;  // reference wind at 20 ft
        low_alt_params(h_ft, W_20, L_u, L_v, L_w, sigma_u, sigma_v, sigma_w);
    } else if (altitude_agl_m >= HIGH_ALT_M) {
        // Medium/high altitude
        L_u = L_v = L_w = HI_SCALE_M;
        sigma_u = sigma_v = sigma_w = severity * SIGMA_MAX_MS;
    } else {
        // Blend region (1000-2000 ft AGL): linear interpolation
        double frac = (altitude_agl_m - LOW_ALT_M) / (HIGH_ALT_M - LOW_ALT_M);

        double W_20 = severity * 30.0;
        double lo_Lu, lo_Lv, lo_Lw, lo_su, lo_sv, lo_sw;
        low_alt_params(h_ft, W_20, lo_Lu, lo_Lv, lo_Lw, lo_su, lo_sv, lo_sw);

        double hi_L = HI_SCALE_M;
        double hi_s = severity * SIGMA_MAX_MS;

        L_u     = lo_Lu + frac * (hi_L - lo_Lu);
        L_v     = lo_Lv + frac * (hi_L - lo_Lv);
        L_w     = lo_Lw + frac * (hi_L - lo_Lw);
        sigma_u = lo_su + frac * (hi_s - lo_su);
        sigma_v = lo_sv + frac * (hi_s - lo_sv);
        sigma_w = lo_sw + frac * (hi_s - lo_sw);
    }

    // Apply forming filters
    state_u_ = filter1(state_u_, dt_sec, tas_ms, L_u, sigma_u, n_u);

    // v-axis: two cascaded first-order filters (approximation of second-order TF)
    state_v1_ = filter1(state_v1_, dt_sec, tas_ms, L_v, sigma_v, n_v);
    state_v2_ = filter1(state_v2_, dt_sec, tas_ms, L_v, 1.0, state_v1_);

    // w-axis: two cascaded first-order filters
    state_w1_ = filter1(state_w1_, dt_sec, tas_ms, L_w, sigma_w, n_w);
    state_w2_ = filter1(state_w2_, dt_sec, tas_ms, L_w, 1.0, state_w1_);

    p.u_ms = state_u_;
    p.v_ms = state_v2_;
    p.w_ms = state_w2_;

    return p;
}

}  // namespace weather_solver
