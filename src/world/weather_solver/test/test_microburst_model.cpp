#include <gtest/gtest.h>
#include "weather_solver/microburst_model.hpp"

#include <cmath>

using weather_solver::MicroburstModel;

static MicroburstModel::HazardParams make_hazard(
    double north, double east, float radius, float shaft_alt, float intensity,
    uint8_t phase = 2, float act_time = 0.0f)
{
    MicroburstModel::HazardParams h;
    h.north_m = north;
    h.east_m  = east;
    h.core_radius_m    = radius;
    h.shaft_altitude_m = shaft_alt;
    h.intensity        = intensity;
    h.lifecycle_phase  = phase;
    h.activation_time_sec = act_time;
    return h;
}

// ── Center downdraft ───────────────────────────────────────────────────────

TEST(MicroburstModel, CenterDowndraft)
{
    std::vector<MicroburstModel::HazardParams> hazards = {
        make_hazard(0.0, 0.0, 500.0f, 300.0f, 15.0f)
    };

    // Aircraft at center, at shaft altitude
    auto w = MicroburstModel::sample(hazards, 0.0, 0.0, 300.0, 100.0);

    // Strong downdraft (wind_down positive = downdraft)
    EXPECT_GT(w.down_ms, 5.0) << "Should have strong downdraft at center";
    // Minimal horizontal (r ≈ 0 clamped to 10m)
    double h_mag = std::sqrt(w.north_ms * w.north_ms + w.east_ms * w.east_ms);
    EXPECT_LT(h_mag, 3.0) << "Horizontal should be small at center";
}

// ── Peak outflow at core radius ────────────────────────────────────────────

TEST(MicroburstModel, PeakOutflow)
{
    std::vector<MicroburstModel::HazardParams> hazards = {
        make_hazard(0.0, 0.0, 500.0f, 300.0f, 15.0f)
    };

    // Aircraft at core_radius distance (east of center), at shaft altitude
    auto w = MicroburstModel::sample(hazards, 0.0, 500.0, 300.0, 100.0);

    // Should have significant horizontal outflow (pushing east, away from center)
    EXPECT_GT(w.east_ms, 3.0) << "Should have outflow pushing away from center";
    EXPECT_NEAR(w.north_ms, 0.0, 1.0) << "North component should be near zero (east offset)";
}

// ── Far field: negligible wind ─────────────────────────────────────────────

TEST(MicroburstModel, FarField)
{
    std::vector<MicroburstModel::HazardParams> hazards = {
        make_hazard(0.0, 0.0, 500.0f, 300.0f, 15.0f)
    };

    // Aircraft at 5× core radius
    auto w = MicroburstModel::sample(hazards, 0.0, 2500.0, 300.0, 100.0);

    double total = std::sqrt(w.north_ms * w.north_ms +
                             w.east_ms * w.east_ms +
                             w.down_ms * w.down_ms);
    // At 5R, radial = λ*(R/r)*(1-exp(-25)) ≈ λ/5 ≈ 3 m/s, downdraft ≈ 0
    EXPECT_LT(total, 4.0) << "Wind should be modest at 5× radius";
}

// ── No hazards → zero ─────────────────────────────────────────────────────

TEST(MicroburstModel, NoHazards)
{
    std::vector<MicroburstModel::HazardParams> empty;
    auto w = MicroburstModel::sample(empty, 100.0, 200.0, 300.0, 100.0);

    EXPECT_DOUBLE_EQ(w.north_ms, 0.0);
    EXPECT_DOUBLE_EQ(w.east_ms, 0.0);
    EXPECT_DOUBLE_EQ(w.down_ms, 0.0);
}

// ── Dormant phase → zero ──────────────────────────────────────────────────

TEST(MicroburstModel, LifecycleDormant)
{
    std::vector<MicroburstModel::HazardParams> hazards = {
        make_hazard(0.0, 0.0, 500.0f, 300.0f, 15.0f, 0)  // phase 0 = Dormant
    };

    auto w = MicroburstModel::sample(hazards, 0.0, 500.0, 300.0, 100.0);

    EXPECT_DOUBLE_EQ(w.north_ms, 0.0);
    EXPECT_DOUBLE_EQ(w.east_ms, 0.0);
    EXPECT_DOUBLE_EQ(w.down_ms, 0.0);
}

// ── Intensifying phase: ~50% at 30 seconds ────────────────────────────────

TEST(MicroburstModel, LifecycleIntensifying)
{
    // Mature reference
    std::vector<MicroburstModel::HazardParams> mature = {
        make_hazard(0.0, 0.0, 500.0f, 300.0f, 15.0f, 2, 0.0f)  // Mature
    };
    auto w_full = MicroburstModel::sample(mature, 0.0, 500.0, 300.0, 100.0);

    // Intensifying, activated at t=70, sampled at t=100 → 30s elapsed → 50%
    std::vector<MicroburstModel::HazardParams> ramping = {
        make_hazard(0.0, 0.0, 500.0f, 300.0f, 15.0f, 1, 70.0f)
    };
    auto w_half = MicroburstModel::sample(ramping, 0.0, 500.0, 300.0, 100.0);

    // Expect roughly 50% of mature values
    double ratio = w_half.east_ms / w_full.east_ms;
    EXPECT_NEAR(ratio, 0.5, 0.05);
}

// ── Multiple microbursts sum linearly ──────────────────────────────────────

TEST(MicroburstModel, MultipleSum)
{
    // Two identical mature microbursts, one north and one south of aircraft
    std::vector<MicroburstModel::HazardParams> hazards = {
        make_hazard(500.0, 0.0, 500.0f, 300.0f, 10.0f),   // north of aircraft
        make_hazard(-500.0, 0.0, 500.0f, 300.0f, 10.0f),   // south of aircraft
    };

    // Aircraft at origin, at shaft altitude
    auto w = MicroburstModel::sample(hazards, 0.0, 0.0, 300.0, 100.0);

    // North pushes south, south pushes north → cancel in north component
    EXPECT_NEAR(w.north_ms, 0.0, 0.1) << "Symmetric microbursts should cancel N/S";
    // Both push east = 0 (along N axis) → east should be ~0
    EXPECT_NEAR(w.east_ms, 0.0, 0.1);
    // Downdraft from both should sum
    EXPECT_GT(w.down_ms, 0.0) << "Downdraft from both should be positive";
}
