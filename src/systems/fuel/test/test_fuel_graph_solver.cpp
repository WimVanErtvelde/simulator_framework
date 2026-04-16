// Generic fuel graph solver tests — no aircraft-specific references.
// Aircraft-specific tests live in src/aircraft/<type>/test/.

#include "fuel/fuel_graph_solver.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

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

/// Write YAML string to temp file, load into solver, clean up.
static bool loadFromString(FuelGraphSolver& s, const std::string& yaml) {
    const char* path = "/tmp/test_fuel_solver_tmp.yaml";
    {
        std::ofstream f(path);
        f << yaml;
    }
    bool result = s.loadTopologyYaml(path);
    std::remove(path);
    return result;
}

// Minimal synthetic topology: tank → line → pump → line → engine_inlet
static const char* SYNTH_YAML = R"(
fuel:
  fuel_type: TEST
  density_kg_per_liter: 0.72

nodes:
  - id: syn_tank
    type: tank
    label: "Test Tank"
    capacity_kg: 50.0
    unusable_kg: 0.0
    arm_m: 0.0
  - id: syn_pump
    type: pump
    label: "Test Pump"
    subtype: engine
    min_rpm_pct: 5.0
  - id: syn_inlet
    type: engine_inlet
    engine_index: 0

connections:
  - id: line_tank_pump
    type: line
    from: syn_tank
    to: syn_pump
  - id: line_pump_inlet
    type: line
    from: syn_pump
    to: syn_inlet
)";

static FuelGraphSolver makeSynth() {
    FuelGraphSolver s;
    bool ok = loadFromString(s, SYNTH_YAML);
    assert(ok && "Failed to load synthetic fuel topology");
    return s;
}

// ─── Tests ──────────────────────────────────────────────────────────

int main() {

    // T1 ── Load synthetic topology, verify counts ───────────────────
    TEST("Synthetic: load topology — 3 nodes, 2 connections");
    {
        FuelGraphSolver s = makeSynth();
        auto& topo = s.getTopology();
        CHECK(topo.nodes.size() == 3);
        CHECK(topo.connections.size() == 2);
    }
    PASS();

    // T2 ── Engine fed when pump running (engine RPM > 0) ────────────
    TEST("Synthetic: engine fed when pump running, tank drains");
    {
        FuelGraphSolver s = makeSynth();
        FdmInputs fdm;
        fdm.engine_rpm_pct = {60.0};
        fdm.engine_fuel_demand_kgs = {0.01};
        s.setFdmInputs(fdm);

        double qty_before = s.getNodeStates().at("syn_tank").quantity_kg;

        for (int i = 0; i < 100; ++i) s.step(0.02);  // 2 seconds

        CHECK(s.getNodeStates().at("syn_inlet").fed);
        double qty_after = s.getNodeStates().at("syn_tank").quantity_kg;
        CHECK(qty_after < qty_before);
    }
    PASS();

    // T3 ── Engine starved when pump not running (RPM = 0) ───────────
    TEST("Synthetic: engine starved when pump not running, tank unchanged");
    {
        FuelGraphSolver s = makeSynth();
        FdmInputs fdm;
        fdm.engine_rpm_pct = {0.0};
        fdm.engine_fuel_demand_kgs = {0.0};
        s.setFdmInputs(fdm);

        double qty_before = s.getNodeStates().at("syn_tank").quantity_kg;

        for (int i = 0; i < 100; ++i) s.step(0.02);

        CHECK(!s.getNodeStates().at("syn_inlet").fed);
        CHECK_NEAR(s.getNodeStates().at("syn_tank").quantity_kg, qty_before, 0.001);
    }
    PASS();

    // ── Summary ─────────────────────────────────────────────────────
    std::cout << "\n" << tests_passed << "/" << tests_total
              << " tests passed." << std::endl;
    return (tests_passed == tests_total) ? 0 : 1;
}
