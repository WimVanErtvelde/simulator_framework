#include "fuel/fuel_graph_solver.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#ifndef TEST_YAML_PATH
#error "TEST_YAML_PATH must be defined at compile time"
#endif

using namespace fuel_graph;

static int tests_passed = 0;
static int tests_total  = 0;

#define TEST(name)                                                            \
    do {                                                                      \
        tests_total++;                                                        \
        std::cout << "T" << tests_total << ": " << (name) << " ... " << std::flush; \
    } while (0)

#define PASS()                        \
    do {                              \
        tests_passed++;               \
        std::cout << "PASS" << std::endl; \
    } while (0)

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cout << "FAIL  (line " << __LINE__ << ": " << #cond << ")" \
                      << std::endl;                                          \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                \
    do {                                                                     \
        if (std::abs((a) - (b)) > (tol)) {                                  \
            std::cout << "FAIL  (line " << __LINE__ << ": " << #a << "="    \
                      << (a) << " expected ~" << (b) << ")" << std::endl;   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

// ─── Helpers ────────────────────────────────────────────────────────

static FuelGraphSolver makeSolver() {
    FuelGraphSolver s;
    bool ok = s.loadTopologyYaml(TEST_YAML_PATH);
    assert(ok && "Failed to load c172_fuel_v2.yaml");
    return s;
}

/// Set engine running with fuel demand.
static void setEngineRunning(FuelGraphSolver& s, double rpm_pct = 60.0, double demand_kgs = 0.01) {
    FdmInputs fdm;
    fdm.engine_rpm_pct = {rpm_pct};
    fdm.engine_fuel_demand_kgs = {demand_kgs};
    s.setFdmInputs(fdm);
}

// ─── Tests ──────────────────────────────────────────────────────────

int main() {

    // T1 ── Load C172 topology, verify counts ────────────────────────
    TEST("Load C172 topology — 7 nodes, 8 connections, 1 selector group");
    {
        FuelGraphSolver s = makeSolver();
        auto& topo = s.getTopology();
        CHECK(topo.nodes.size() == 7);
        CHECK(topo.connections.size() == 8);  // 2 selector + 5 line + 1 gravity feed
        CHECK(topo.selectors.size() == 1);
    }
    PASS();

    // T2 ── Selector BOTH — both tanks feed engine ───────────────────
    TEST("Selector BOTH — both tanks drain equally");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s);
        // Default selector is BOTH

        double left_before  = s.getNodeStates().at("tank_left").quantity_kg;
        double right_before = s.getNodeStates().at("tank_right").quantity_kg;

        for (int i = 0; i < 100; ++i) s.step(0.02);  // 2 seconds

        double left_after  = s.getNodeStates().at("tank_left").quantity_kg;
        double right_after = s.getNodeStates().at("tank_right").quantity_kg;

        CHECK(s.getNodeStates().at("engine_inlet_0").fed);
        CHECK(left_after < left_before);
        CHECK(right_after < right_before);
        // Equal drain: both tanks should lose same amount
        CHECK_NEAR(left_before - left_after, right_before - right_after, 0.001);
    }
    PASS();

    // T3 ── Selector LEFT — only left tank drains ────────────────────
    TEST("Selector LEFT — only left tank drains, right unchanged");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s);
        s.setSelector("sel_fuel", "LEFT");

        double right_before = s.getNodeStates().at("tank_right").quantity_kg;

        for (int i = 0; i < 100; ++i) s.step(0.02);

        CHECK(s.getNodeStates().at("engine_inlet_0").fed);
        CHECK(s.getNodeStates().at("tank_left").quantity_kg < 92.0);
        CHECK_NEAR(s.getNodeStates().at("tank_right").quantity_kg, right_before, 0.001);
    }
    PASS();

    // T4 ── Selector RIGHT — only right tank drains ──────────────────
    TEST("Selector RIGHT — only right tank drains, left unchanged");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s);
        s.setSelector("sel_fuel", "RIGHT");

        double left_before = s.getNodeStates().at("tank_left").quantity_kg;

        for (int i = 0; i < 100; ++i) s.step(0.02);

        CHECK(s.getNodeStates().at("engine_inlet_0").fed);
        CHECK_NEAR(s.getNodeStates().at("tank_left").quantity_kg, left_before, 0.001);
        CHECK(s.getNodeStates().at("tank_right").quantity_kg < 92.0);
    }
    PASS();

    // T5 ── Selector OFF — engine starved ────────────────────────────
    TEST("Selector OFF — engine starved, no tank drain");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s);
        s.setSelector("sel_fuel", "OFF");

        double left_before  = s.getNodeStates().at("tank_left").quantity_kg;
        double right_before = s.getNodeStates().at("tank_right").quantity_kg;

        for (int i = 0; i < 100; ++i) s.step(0.02);

        CHECK(!s.getNodeStates().at("engine_inlet_0").fed);
        CHECK_NEAR(s.getNodeStates().at("tank_left").quantity_kg, left_before, 0.001);
        CHECK_NEAR(s.getNodeStates().at("tank_right").quantity_kg, right_before, 0.001);
    }
    PASS();

    // T6 ── Mechanical pump fail — engine still fed via gravity ────────
    TEST("Mechanical pump fail — engine still fed (C172 gravity feed)");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s);
        s.applyFailureEffect("pump_mechanical", "force", "online", "false");

        s.step(0.02);

        CHECK(s.getNodeStates().at("engine_inlet_0").fed);
    }
    PASS();

    // T7 ── Both pumps fail — engine still fed via gravity ───────────
    TEST("Both pumps fail — engine still fed (C172 high-wing gravity)");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s);
        s.applyFailureEffect("pump_mechanical", "force", "online", "false");
        s.applyFailureEffect("pump_electric", "force", "online", "false");
        s.commandPump("sw_fuel_pump", true);  // switch on but forced offline

        s.step(0.02);

        CHECK(s.getNodeStates().at("engine_inlet_0").fed);
    }
    PASS();

    // T8 ── Selector OFF still starves despite gravity feed ────────────
    TEST("Selector OFF — engine starved even with gravity feed path");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s);
        s.setSelector("sel_fuel", "OFF");

        s.step(0.02);

        // Gravity feed goes from strainer to inlet, but selector OFF
        // means no fuel reaches strainer → gravity path is dry too
        CHECK(!s.getNodeStates().at("engine_inlet_0").fed);
    }
    PASS();

    // T9 ── Fuel exhaustion + selector switch ────────────────────────
    TEST("Fuel exhaustion LEFT → switch to BOTH → engine fed from right");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s, 60.0, 10.0);  // very high demand for quick drain
        s.setSelector("sel_fuel", "LEFT");

        // Drain left tank to unusable: 90.6 usable kg / 10 kg/s = ~9s → 500 steps
        for (int i = 0; i < 500; ++i) s.step(0.02);

        // Left tank should be at or near unusable
        double left_qty = s.getNodeStates().at("tank_left").quantity_kg;
        CHECK_NEAR(left_qty, 1.4, 0.5);  // unusable_kg = 1.4
        CHECK(!s.getNodeStates().at("engine_inlet_0").fed);

        // Switch to BOTH — right tank still has fuel
        s.setSelector("sel_fuel", "BOTH");
        s.step(0.02);

        CHECK(s.getNodeStates().at("engine_inlet_0").fed);
    }
    PASS();

    // T10 ── Fuel leak ───────────────────────────────────────────────
    TEST("Fuel leak — upstream tank loses fuel beyond engine consumption");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s, 60.0, 0.001);  // very low engine demand
        s.setSelector("sel_fuel", "LEFT");

        // Apply leak on strainer→manifold at 100 L/h (high rate for fast test)
        s.applyFailureEffect("line_strainer_to_manifold", "set", "leak_rate_lph", "100.0");

        double left_before = s.getNodeStates().at("tank_left").quantity_kg;

        // Step for 1 second (50 steps × 0.02s)
        for (int i = 0; i < 50; ++i) s.step(0.02);

        double left_after = s.getNodeStates().at("tank_left").quantity_kg;
        double drain = left_before - left_after;
        // Expected leak drain: 100 L/h × 0.72 kg/L / 3600 s = 0.02 kg/s × 1s = 0.02 kg
        // Plus tiny engine consumption: 0.001 kg/s × 1s = 0.001 kg
        // Total ~0.021 kg. But mostly from leak.
        CHECK(drain > 0.015);  // Leak dominates
        // Right tank unchanged (only LEFT selected)
        CHECK_NEAR(s.getNodeStates().at("tank_right").quantity_kg, 92.0, 0.001);
    }
    PASS();

    // T11 ── Jam selector ────────────────────────────────────────────
    TEST("Jam selector in LEFT — command to BOTH ignored, still LEFT");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s);
        s.setSelector("sel_fuel", "LEFT");
        s.step(0.02);

        // Jam the selector
        s.applyFailureEffect("sel_fuel", "jam", "jammed", "true");

        // Try to change to BOTH
        s.setSelector("sel_fuel", "BOTH");
        s.step(0.02);

        // Right tank should not be feeding (still LEFT)
        double right_before = s.getNodeStates().at("tank_right").quantity_kg;
        for (int i = 0; i < 50; ++i) s.step(0.02);
        CHECK_NEAR(s.getNodeStates().at("tank_right").quantity_kg, right_before, 0.001);
        // Left tank drains
        CHECK(s.getNodeStates().at("tank_left").quantity_kg < 92.0);
    }
    PASS();

    // T12 ── CAS: LOW FUEL ───────────────────────────────────────────
    TEST("CAS LOW FUEL — drain both tanks below threshold");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s, 60.0, 5.0);  // very high demand

        // Drain both tanks: 184 kg total, 5 kg/s → ~36 seconds
        for (int i = 0; i < 2000; ++i) s.step(0.02);  // 40 seconds

        bool found = false;
        for (auto& msg : s.getCasMessages()) {
            if (msg.text == "LOW FUEL") { found = true; break; }
        }
        CHECK(found);
    }
    PASS();

    // T13 ── CAS: FUEL PRESS ─────────────────────────────────────────
    TEST("CAS FUEL PRESS — engine starved");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s);
        s.setSelector("sel_fuel", "OFF");

        s.step(0.02);

        bool found = false;
        for (auto& msg : s.getCasMessages()) {
            if (msg.text == "FUEL PRESS") { found = true; break; }
        }
        CHECK(found);
    }
    PASS();

    // T14 ── Reset ───────────────────────────────────────────────────
    TEST("Reset — tanks full, failures cleared, selector at default");
    {
        FuelGraphSolver s = makeSolver();
        setEngineRunning(s, 60.0, 1.0);
        s.setSelector("sel_fuel", "LEFT");
        s.applyFailureEffect("pump_mechanical", "force", "online", "false");

        // Drain some fuel
        for (int i = 0; i < 100; ++i) s.step(0.02);

        s.reset();

        // Tanks at initial capacity
        CHECK_NEAR(s.getNodeStates().at("tank_left").quantity_kg, 92.0, 0.01);
        CHECK_NEAR(s.getNodeStates().at("tank_right").quantity_kg, 92.0, 0.01);
        // Failures cleared
        CHECK(s.getCasMessages().empty());
        // Need to step after reset for state to be evaluated
        setEngineRunning(s);
        s.step(0.02);
        CHECK(s.getNodeStates().at("engine_inlet_0").fed);
    }
    PASS();

    // T15 ── No engine demand — tanks should not drain ───────────────
    TEST("No engine demand — RPM high, pumps running, tanks don't drain");
    {
        FuelGraphSolver s = makeSolver();
        FdmInputs fdm;
        fdm.engine_rpm_pct = {60.0};
        fdm.engine_fuel_demand_kgs = {0.0};  // zero demand
        s.setFdmInputs(fdm);

        double left_before  = s.getNodeStates().at("tank_left").quantity_kg;
        double right_before = s.getNodeStates().at("tank_right").quantity_kg;

        for (int i = 0; i < 100; ++i) s.step(0.02);

        CHECK_NEAR(s.getNodeStates().at("tank_left").quantity_kg, left_before, 0.001);
        CHECK_NEAR(s.getNodeStates().at("tank_right").quantity_kg, right_before, 0.001);
        // Engine is still "fed" — path exists, just no consumption
        CHECK(s.getNodeStates().at("engine_inlet_0").fed);
    }
    PASS();

    // ── Summary ─────────────────────────────────────────────────────
    std::cout << "\n" << tests_passed << "/" << tests_total
              << " tests passed." << std::endl;
    return (tests_passed == tests_total) ? 0 : 1;
}
