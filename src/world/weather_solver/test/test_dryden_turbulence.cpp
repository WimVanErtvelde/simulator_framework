#include <gtest/gtest.h>
#include "weather_solver/dryden_turbulence.hpp"

#include <cmath>
#include <vector>

using weather_solver::DrydenTurbulence;

// ── Determinism: same seed → identical output ──────────────────────────────

TEST(DrydenTurbulence, Determinism)
{
    DrydenTurbulence d1, d2;
    DrydenTurbulence::Config cfg{DrydenTurbulence::Model::MIL_F_8785C, 42};
    d1.configure(cfg);
    d2.configure(cfg);

    for (int i = 0; i < 1000; ++i) {
        auto p1 = d1.update(0.02, 0.5, 300.0, 60.0);
        auto p2 = d2.update(0.02, 0.5, 300.0, 60.0);
        EXPECT_DOUBLE_EQ(p1.u_ms, p2.u_ms) << "Mismatch at step " << i;
        EXPECT_DOUBLE_EQ(p1.v_ms, p2.v_ms) << "Mismatch at step " << i;
        EXPECT_DOUBLE_EQ(p1.w_ms, p2.w_ms) << "Mismatch at step " << i;
    }
}

// ── Zero severity → zero output ────────────────────────────────────────────

TEST(DrydenTurbulence, ZeroSeverity)
{
    DrydenTurbulence d;
    d.configure({DrydenTurbulence::Model::MIL_F_8785C, 123});

    for (int i = 0; i < 100; ++i) {
        auto p = d.update(0.02, 0.0, 300.0, 60.0);
        EXPECT_EQ(p.u_ms, 0.0);
        EXPECT_EQ(p.v_ms, 0.0);
        EXPECT_EQ(p.w_ms, 0.0);
    }
}

// ── Model NONE → zero output regardless of severity ────────────────────────

TEST(DrydenTurbulence, NoneModel)
{
    DrydenTurbulence d;
    d.configure({DrydenTurbulence::Model::NONE, 99});

    for (int i = 0; i < 100; ++i) {
        auto p = d.update(0.02, 1.0, 300.0, 60.0);
        EXPECT_EQ(p.u_ms, 0.0);
        EXPECT_EQ(p.v_ms, 0.0);
        EXPECT_EQ(p.w_ms, 0.0);
    }
}

// ── Magnitude scaling: severity 0.5 → RMS roughly half of severity 1.0 ────

TEST(DrydenTurbulence, MagnitudeScaling)
{
    const int N = 10000;
    const double dt = 0.02;
    const double alt = 500.0;
    const double tas = 60.0;

    auto compute_rms = [&](double severity) {
        DrydenTurbulence d;
        d.configure({DrydenTurbulence::Model::MIL_F_8785C, 42});
        double sum_sq = 0.0;
        for (int i = 0; i < N; ++i) {
            auto p = d.update(dt, severity, alt, tas);
            sum_sq += p.u_ms * p.u_ms + p.v_ms * p.v_ms + p.w_ms * p.w_ms;
        }
        return std::sqrt(sum_sq / N);
    };

    double rms_half = compute_rms(0.5);
    double rms_full = compute_rms(1.0);

    EXPECT_GT(rms_full, 0.0) << "Full severity should produce nonzero output";
    EXPECT_GT(rms_half, 0.0) << "Half severity should produce nonzero output";

    double ratio = rms_half / rms_full;
    // Expect ratio roughly 0.5, allow 40% tolerance (statistical + nonlinear scaling)
    EXPECT_GT(ratio, 0.2) << "Half severity RMS should be roughly half of full";
    EXPECT_LT(ratio, 0.8) << "Half severity RMS should be roughly half of full";
}

// ── Altitude scaling: low vs high altitude produce different magnitudes ─────

TEST(DrydenTurbulence, AltitudeScaling)
{
    const int N = 5000;
    const double dt = 0.02;
    const double tas = 60.0;

    auto compute_rms = [&](double alt_agl) {
        DrydenTurbulence d;
        d.configure({DrydenTurbulence::Model::MIL_F_8785C, 42});
        double sum_sq = 0.0;
        for (int i = 0; i < N; ++i) {
            auto p = d.update(dt, 0.5, alt_agl, tas);
            sum_sq += p.u_ms * p.u_ms + p.v_ms * p.v_ms + p.w_ms * p.w_ms;
        }
        return std::sqrt(sum_sq / N);
    };

    double rms_low  = compute_rms(30.0);    // ~100 ft AGL
    double rms_high = compute_rms(1500.0);  // ~5000 ft AGL

    EXPECT_GT(rms_low, 0.0);
    EXPECT_GT(rms_high, 0.0);
    // They should differ (different scale lengths / sigmas)
    EXPECT_NE(rms_low, rms_high) << "Different altitudes should produce different RMS";
}
