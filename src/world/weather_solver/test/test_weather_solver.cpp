#include <gtest/gtest.h>
#include "weather_solver/weather_solver.hpp"

#include <cmath>
#include <sim_msgs/msg/weather_patch.hpp>

using weather_solver::WeatherSolver;

// ── ISA defaults (no weather set) ──────────────────────────────────────────

TEST(WeatherSolver, IsaDefaults)
{
    WeatherSolver s;
    s.configure({});

    auto r = s.compute(0.02, 0.0, 0.0, 60.0);

    // Sea-level ISA
    EXPECT_NEAR(r.temperature_k, 288.15, 0.01);
    EXPECT_NEAR(r.pressure_pa, 101325.0, 1.0);
    EXPECT_NEAR(r.oat_k, 288.15, 0.01);
    EXPECT_NEAR(r.qnh_pa, 101325.0, 1.0);
    EXPECT_NEAR(r.density_kgm3, 1.225, 0.001);

    // No wind
    EXPECT_DOUBLE_EQ(r.wind_north_ms, 0.0);
    EXPECT_DOUBLE_EQ(r.wind_east_ms, 0.0);
    EXPECT_DOUBLE_EQ(r.wind_down_ms, 0.0);
}

// ── ISA at altitude ────────────────────────────────────────────────────────

TEST(WeatherSolver, IsaAtAltitude)
{
    WeatherSolver s;
    s.configure({});

    // At 5000m, ISA temp = 288.15 - 0.0065 * 5000 = 255.65 K
    auto r = s.compute(0.02, 5000.0, 5000.0, 80.0);
    EXPECT_NEAR(r.temperature_k, 255.65, 0.01);
    EXPECT_NEAR(r.oat_k, 255.65, 0.01);
}

// ── Temperature deviation ──────────────────────────────────────────────────

TEST(WeatherSolver, TemperatureDeviation)
{
    WeatherSolver s;
    s.configure({});

    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 303.15;  // ISA+15
    w.pressure_sl_pa = 101325.0;
    w.turbulence_model = 0;  // None
    s.set_weather(w);

    auto r = s.compute(0.02, 0.0, 0.0, 60.0);
    EXPECT_NEAR(r.oat_k, 303.15, 0.01);  // ISA + 15K deviation
    EXPECT_NEAR(r.temperature_k, 288.15, 0.01);  // ISA T unchanged
}

// ── Single wind layer ──────────────────────────────────────────────────────

TEST(WeatherSolver, SingleWindLayer)
{
    WeatherSolver s;
    s.configure({});

    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 288.15;
    w.pressure_sl_pa = 101325.0;
    w.turbulence_model = 0;

    sim_msgs::msg::WeatherWindLayer wl;
    wl.altitude_msl_m = 0.0f;
    wl.wind_speed_ms = 10.0f;
    wl.wind_direction_deg = 270.0f;  // FROM west → blows east
    wl.vertical_wind_ms = 0.0f;
    wl.turbulence_severity = 0.0f;
    w.wind_layers.push_back(wl);

    s.set_weather(w);
    auto r = s.compute(0.02, 0.0, 0.0, 60.0);

    // FROM 270° (west): north = -10*cos(270°) ≈ 0, east = -10*sin(270°) = +10
    EXPECT_NEAR(r.wind_north_ms, 0.0, 0.1);
    EXPECT_NEAR(r.wind_east_ms, 10.0, 0.1);
    EXPECT_NEAR(r.wind_down_ms, 0.0, 0.01);
}

// ── Two wind layers with altitude interpolation ────────────────────────────

TEST(WeatherSolver, WindInterpolation)
{
    WeatherSolver s;
    s.configure({});

    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 288.15;
    w.pressure_sl_pa = 101325.0;
    w.turbulence_model = 0;

    sim_msgs::msg::WeatherWindLayer lo, hi;
    lo.altitude_msl_m = 0.0f;
    lo.wind_speed_ms = 5.0f;
    lo.wind_direction_deg = 180.0f;  // FROM south
    lo.vertical_wind_ms = 0.0f;
    lo.turbulence_severity = 0.0f;

    hi.altitude_msl_m = 3000.0f;
    hi.wind_speed_ms = 15.0f;
    hi.wind_direction_deg = 270.0f;  // FROM west
    hi.vertical_wind_ms = 0.0f;
    hi.turbulence_severity = 0.0f;

    w.wind_layers.push_back(lo);
    w.wind_layers.push_back(hi);
    s.set_weather(w);

    // At 1500m (midpoint): speed ≈ 10, direction ≈ 225° (shortest arc 180→270)
    auto r = s.compute(0.02, 1500.0, 1500.0, 70.0);

    double total_speed = std::sqrt(r.wind_north_ms * r.wind_north_ms +
                                   r.wind_east_ms * r.wind_east_ms);
    EXPECT_NEAR(total_speed, 10.0, 0.5);

    // Direction should be roughly 225° (FROM south-west → blows north-east)
    // At 225° FROM: north = -10*cos(225°) = +7.07, east = -10*sin(225°) = +7.07
    EXPECT_GT(r.wind_north_ms, 0.0);  // blows toward north
    EXPECT_GT(r.wind_east_ms, 0.0);   // blows toward east
}

// ── Turbulence adds perturbation to wind ───────────────────────────────────

TEST(WeatherSolver, TurbulenceAddsPerturbation)
{
    WeatherSolver s;
    s.configure({});

    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 288.15;
    w.pressure_sl_pa = 101325.0;
    w.turbulence_model = 1;  // MIL_F_8785C
    w.deterministic_seed = 42;

    sim_msgs::msg::WeatherWindLayer wl;
    wl.altitude_msl_m = 0.0f;
    wl.wind_speed_ms = 0.0f;      // zero ambient wind
    wl.wind_direction_deg = 0.0f;
    wl.vertical_wind_ms = 0.0f;
    wl.turbulence_severity = 0.8f;
    w.wind_layers.push_back(wl);

    s.set_weather(w);

    // Run 1000 steps, collect wind magnitudes
    double max_mag = 0.0;
    for (int i = 0; i < 1000; ++i) {
        auto r = s.compute(0.02, 300.0, 300.0, 60.0);
        double mag = std::sqrt(r.wind_north_ms * r.wind_north_ms +
                               r.wind_east_ms * r.wind_east_ms +
                               r.wind_down_ms * r.wind_down_ms);
        if (mag > max_mag) max_mag = mag;
    }

    // With severity=0.8, we expect some nonzero perturbation
    EXPECT_GT(max_mag, 0.1) << "Turbulence should produce nonzero wind perturbation";
}

// ── Angular wrap: 350° → 010° interpolation goes through 360°/0° ──────────

TEST(WeatherSolver, WindDirectionWrap)
{
    WeatherSolver s;
    s.configure({});

    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 288.15;
    w.pressure_sl_pa = 101325.0;
    w.turbulence_model = 0;

    sim_msgs::msg::WeatherWindLayer lo, hi;
    lo.altitude_msl_m = 0.0f;
    lo.wind_speed_ms = 10.0f;
    lo.wind_direction_deg = 350.0f;

    hi.altitude_msl_m = 1000.0f;
    hi.wind_speed_ms = 10.0f;
    hi.wind_direction_deg = 10.0f;

    w.wind_layers.push_back(lo);
    w.wind_layers.push_back(hi);
    s.set_weather(w);

    // At 500m (midpoint): direction should be 0° (360°), not 180°
    auto r = s.compute(0.02, 500.0, 500.0, 60.0);

    // FROM 0° (north): north = -10*cos(0) = -10, east = -10*sin(0) = 0
    EXPECT_NEAR(r.wind_north_ms, -10.0, 0.5);
    EXPECT_NEAR(r.wind_east_ms, 0.0, 0.5);
}

// ── Gust modulation ────────────────────────────────────────────────────────

// Helper to build a single-layer wind scenario for gust tests.
static sim_msgs::msg::WeatherState make_gust_scenario(
    float sustained_ms, float gust_ms, uint32_t seed)
{
    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 288.15;
    w.pressure_sl_pa = 101325.0;
    w.turbulence_model = 0;         // Dryden off for pure gust observation
    w.deterministic_seed = seed;

    sim_msgs::msg::WeatherWindLayer wl;
    wl.altitude_msl_m = 0.0f;
    wl.wind_speed_ms = sustained_ms;
    wl.wind_direction_deg = 270.0f;  // FROM west → east component = +sustained
    wl.gust_speed_ms = gust_ms;
    wl.turbulence_severity = 0.0f;
    w.wind_layers.push_back(wl);
    return w;
}

TEST(WeatherSolver, GustNoModulationWhenPeakEqualsBase)
{
    WeatherSolver s;
    s.configure({});
    s.set_weather(make_gust_scenario(10.0f, 10.0f, 42));

    // 60 s at 50 Hz: east should stay steady at ~10 m/s
    double min_east = 100.0, max_east = -100.0;
    for (int i = 0; i < 3000; ++i) {
        auto r = s.compute(0.02, 0.0, 0.0, 60.0);
        min_east = std::min(min_east, r.wind_east_ms);
        max_east = std::max(max_east, r.wind_east_ms);
    }
    EXPECT_NEAR(min_east, 10.0, 0.1);
    EXPECT_NEAR(max_east, 10.0, 0.1);
}

TEST(WeatherSolver, GustModulatesWhenPeakAboveBase)
{
    WeatherSolver s;
    s.configure({});
    s.set_weather(make_gust_scenario(10.0f, 25.0f, 42));

    // Run 120 s at 50 Hz. Max idle is 45 s, cycle length ~4 s, so at least
    // one full peak should land in-window.
    double min_east = 100.0, max_east = -100.0;
    for (int i = 0; i < 6000; ++i) {
        auto r = s.compute(0.02, 0.0, 0.0, 60.0);
        min_east = std::min(min_east, r.wind_east_ms);
        max_east = std::max(max_east, r.wind_east_ms);
    }
    // Between gusts, east ≈ 10; at peak, east ≈ 25.
    EXPECT_NEAR(min_east, 10.0, 0.1);
    EXPECT_GT(max_east, 24.0);
    EXPECT_LE(max_east, 25.5);
}

TEST(WeatherSolver, GustDeterministicSameSeedSamePattern)
{
    WeatherSolver s1, s2;
    s1.configure({});
    s2.configure({});
    s1.set_weather(make_gust_scenario(10.0f, 25.0f, 12345));
    s2.set_weather(make_gust_scenario(10.0f, 25.0f, 12345));

    // Run 120 s, compare every 50th sample (once per sim-second).
    for (int i = 0; i < 6000; ++i) {
        auto r1 = s1.compute(0.02, 0.0, 0.0, 60.0);
        auto r2 = s2.compute(0.02, 0.0, 0.0, 60.0);
        if (i % 50 == 0) {
            EXPECT_NEAR(r1.wind_east_ms, r2.wind_east_ms, 1e-6);
        }
    }
}

TEST(WeatherSolver, GustIdleAtStart)
{
    WeatherSolver s;
    s.configure({});
    s.set_weather(make_gust_scenario(10.0f, 25.0f, 42));

    // Modulator starts in IDLE for a random 15–45 s window. First compute
    // should therefore return the sustained wind with no gust delta.
    auto r = s.compute(0.02, 0.0, 0.0, 60.0);
    EXPECT_NEAR(r.wind_east_ms, 10.0, 0.01);
}

// ── Patch overrides (Slice 5b-iv-a) ────────────────────────────────────────

TEST(WeatherSolver, PatchTemperatureOverrideWhenInside)
{
    WeatherSolver s;
    s.configure({});

    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 288.15f;   // Global 15 °C at SL
    w.pressure_sl_pa   = 101325.0f;
    w.turbulence_model = 0;

    sim_msgs::msg::WeatherPatch p;
    p.patch_id = 1;
    p.lat_deg  = 50.9014;
    p.lon_deg  = 4.4844;
    p.radius_m = 10000.0f;          // 10 km
    p.override_temperature = true;
    p.temperature_k = 313.15f;      // 40 °C SL
    w.patches.push_back(p);

    s.set_weather(w);

    // Aircraft at patch center, sea level
    auto r_inside = s.compute(0.02, 0.0, 0.0, 60.0, 50.9014, 4.4844, 0.0);
    EXPECT_NEAR(r_inside.oat_k, 313.15, 0.5);

    // Aircraft ~55 km away, outside the 10 km patch
    auto r_outside = s.compute(0.02, 0.0, 0.0, 60.0, 51.4, 4.4844, 0.0);
    EXPECT_NEAR(r_outside.oat_k, 288.15, 0.5);
}

TEST(WeatherSolver, PatchWindOverrideReplacesLayers)
{
    WeatherSolver s;
    s.configure({});

    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 288.15f;
    w.pressure_sl_pa   = 101325.0f;
    w.turbulence_model = 0;

    // Global wind: FROM 270° at 10 m/s → blows east, +wind_east_ms
    sim_msgs::msg::WeatherWindLayer gl;
    gl.altitude_msl_m     = 0.0f;
    gl.wind_direction_deg = 270.0f;
    gl.wind_speed_ms      = 10.0f;
    w.wind_layers.push_back(gl);

    // Patch wind: FROM 090° at 20 m/s → blows west, -wind_east_ms
    sim_msgs::msg::WeatherPatch p;
    p.patch_id = 1;
    p.lat_deg  = 50.9014;
    p.lon_deg  = 4.4844;
    p.radius_m = 10000.0f;
    sim_msgs::msg::WeatherWindLayer pl;
    pl.altitude_msl_m     = 0.0f;
    pl.wind_direction_deg = 90.0f;
    pl.wind_speed_ms      = 20.0f;
    p.wind_layers.push_back(pl);
    w.patches.push_back(p);

    s.set_weather(w);

    // Inside — patch wind applies, ~ -20 m/s east
    auto r_inside = s.compute(0.02, 0.0, 0.0, 60.0, 50.9014, 4.4844, 0.0);
    EXPECT_NEAR(r_inside.wind_east_ms, -20.0, 0.5);

    // Outside — global wind applies, ~ +10 m/s east
    auto r_outside = s.compute(0.02, 0.0, 0.0, 60.0, 51.4, 4.4844, 0.0);
    EXPECT_NEAR(r_outside.wind_east_ms, 10.0, 0.5);
}

TEST(WeatherSolver, SmallestPatchWinsWhenOverlapping)
{
    WeatherSolver s;
    s.configure({});

    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 288.15f;
    w.pressure_sl_pa   = 101325.0f;
    w.turbulence_model = 0;

    sim_msgs::msg::WeatherPatch big;
    big.patch_id = 1;
    big.lat_deg  = 50.9;
    big.lon_deg  = 4.5;
    big.radius_m = 20000.0f;
    big.override_temperature = true;
    big.temperature_k = 293.15f;    // 20 °C
    w.patches.push_back(big);

    sim_msgs::msg::WeatherPatch small;
    small.patch_id = 2;
    small.lat_deg  = 50.9;
    small.lon_deg  = 4.5;
    small.radius_m = 5000.0f;
    small.override_temperature = true;
    small.temperature_k = 303.15f;  // 30 °C — smallest-radius winner
    w.patches.push_back(small);

    s.set_weather(w);

    auto r = s.compute(0.02, 0.0, 0.0, 60.0, 50.9, 4.5, 0.0);
    EXPECT_NEAR(r.oat_k, 303.15, 0.5);
}

TEST(WeatherSolver, PatchOverrideFlagOffDoesNotApply)
{
    WeatherSolver s;
    s.configure({});

    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 288.15f;
    w.pressure_sl_pa   = 101325.0f;

    sim_msgs::msg::WeatherPatch p;
    p.patch_id = 1;
    p.lat_deg  = 50.9;
    p.lon_deg  = 4.5;
    p.radius_m = 10000.0f;
    p.override_temperature = false;  // flag OFF
    p.temperature_k = 313.15f;       // value populated but ignored
    w.patches.push_back(p);

    s.set_weather(w);

    auto r = s.compute(0.02, 0.0, 0.0, 60.0, 50.9, 4.5, 0.0);
    EXPECT_NEAR(r.oat_k, 288.15, 0.5);  // global wins
}

// ── Patch pressure/runway overrides (Slice 5b-iv) ──────────────────────────

TEST(WeatherSolver, PatchPressureOverrideWhenInside)
{
    WeatherSolver s;
    s.configure({});

    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 288.15f;
    w.pressure_sl_pa   = 101325.0f;
    w.turbulence_model = 0;

    sim_msgs::msg::WeatherPatch p;
    p.patch_id = 1;
    p.lat_deg  = 50.9014;
    p.lon_deg  = 4.4844;
    p.radius_m = 10000.0f;
    p.override_pressure = true;
    p.pressure_sl_pa    = 99000.0f;  // ~990 hPa low pressure
    w.patches.push_back(p);

    s.set_weather(w);

    // Inside patch — altimeter-only shift
    auto r_inside = s.compute(0.02, 0.0, 0.0, 60.0, 50.9014, 4.4844, 0.0);
    EXPECT_NEAR(r_inside.qnh_pa, 99000.0, 10.0);
    // Actual pressure/density untouched (field QNH is altimeter setting,
    // not density physics).
    EXPECT_NEAR(r_inside.pressure_pa, 101325.0, 1.0);
    EXPECT_NEAR(r_inside.density_kgm3, 1.225, 0.001);

    // Outside patch — global QNH wins
    auto r_outside = s.compute(0.02, 0.0, 0.0, 60.0, 51.4, 4.4844, 0.0);
    EXPECT_NEAR(r_outside.qnh_pa, 101325.0, 10.0);
}

TEST(WeatherSolver, PatchPressureOverrideFlagOffIgnored)
{
    WeatherSolver s;
    s.configure({});

    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 288.15f;
    w.pressure_sl_pa   = 101325.0f;

    sim_msgs::msg::WeatherPatch p;
    p.patch_id = 1;
    p.lat_deg  = 50.9;
    p.lon_deg  = 4.5;
    p.radius_m = 10000.0f;
    p.override_pressure = false;
    p.pressure_sl_pa    = 99000.0f;  // populated but ignored
    w.patches.push_back(p);

    s.set_weather(w);

    auto r = s.compute(0.02, 0.0, 0.0, 60.0, 50.9, 4.5, 0.0);
    EXPECT_NEAR(r.qnh_pa, 101325.0, 10.0);  // global wins
}

TEST(WeatherSolver, PatchRunwayOverrideWhenInside)
{
    WeatherSolver s;
    s.configure({});

    sim_msgs::msg::WeatherState w;
    w.temperature_sl_k = 288.15f;
    w.pressure_sl_pa   = 101325.0f;
    w.runway_friction  = 0;  // global DRY

    sim_msgs::msg::WeatherPatch p;
    p.patch_id = 1;
    p.lat_deg  = 50.9;
    p.lon_deg  = 4.5;
    p.radius_m = 10000.0f;
    p.override_runway    = true;
    p.runway_friction    = 12;  // ICE+MEDIUM (example index)
    w.patches.push_back(p);

    s.set_weather(w);

    // Inside patch — patch friction wins
    auto r_inside = s.compute(0.02, 0.0, 0.0, 60.0, 50.9, 4.5, 0.0);
    EXPECT_EQ(r_inside.effective_runway_friction, 12);

    // Outside — global wins
    auto r_outside = s.compute(0.02, 0.0, 0.0, 60.0, 51.4, 4.5, 0.0);
    EXPECT_EQ(r_outside.effective_runway_friction, 0);
}
