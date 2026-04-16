// Generic graph solver tests — no aircraft-specific references.
// Aircraft-specific tests live in src/aircraft/<type>/test/.

#include "electrical/graph_solver.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

using namespace elec_graph;

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

/// Write yaml content to a temp file, try to load it, clean up. Returns loadTopologyYaml result.
static bool loadFromString(GraphSolver& s, const std::string& yaml) {
    const char* path = "/tmp/test_graph_solver_tmp.yaml";
    {
        std::ofstream f(path);
        f << yaml;
    }
    bool result = s.loadTopologyYaml(path);
    std::remove(path);
    return result;
}

// Minimal synthetic topology: battery → switch → bus → wire → load
static const char* SYNTH_YAML = R"(
aircraft_label: "Synthetic test topology"
nodes:
  - id: bat
    type: source
    subtype: battery
    nominal_voltage: 24.0
    battery: { capacity_ah: 10, initial_soc: 100 }
  - id: bus
    type: bus
    label: "Main Bus"
  - id: lamp
    type: load
    label: "Lamp"
    nominal_current: 2.0
connections:
  - id: sw_main
    type: switch
    from: bat
    to: bus
    default_closed: false
    pilot_controllable: true
  - id: wire_lamp
    type: wire
    from: bus
    to: lamp
)";

static GraphSolver makeSynth() {
    GraphSolver s;
    bool ok = loadFromString(s, SYNTH_YAML);
    assert(ok && "Failed to load synthetic topology");
    return s;
}

// ─── Synthetic topology tests ───────────────────────────────────────

int main() {

    // T1 ── BFS propagation: switch closed → load powered ────────────
    TEST("Synthetic: switch closed -> load powered via BFS");
    {
        GraphSolver s = makeSynth();
        s.commandConnection("sw_main", 1);
        s.step(0.02);
        auto& ns = s.getNodeStates();
        CHECK(ns.at("bus").powered);
        CHECK(ns.at("lamp").powered);
        CHECK_NEAR(ns.at("bus").voltage, 24.0, 1.5);
    }
    PASS();

    // T2 ── Switch open → load unpowered ─────────────────────────────
    TEST("Synthetic: switch open -> load unpowered");
    {
        GraphSolver s = makeSynth();
        s.step(0.02);
        auto& ns = s.getNodeStates();
        CHECK(!ns.at("bus").powered);
        CHECK(!ns.at("lamp").powered);
    }
    PASS();

    // T3 ── Battery SOC drain with load drawing current ──────────────
    TEST("Synthetic: battery SOC drains when load draws current");
    {
        GraphSolver s = makeSynth();
        s.commandConnection("sw_main", 1);
        s.step(0.02);
        double soc_before = s.getNodeStates().at("bat").battery_soc;

        for (int i = 0; i < 200; ++i)
            s.step(0.02);

        double soc_after = s.getNodeStates().at("bat").battery_soc;
        CHECK(soc_after < soc_before);
    }
    PASS();

    // ─── YAML validation tests ──────────────────────────────────────

    // T4 ── Duplicate node ID → load fails ───────────────────────────
    TEST("Duplicate node ID → loadTopology returns false");
    {
        GraphSolver s;
        bool ok = loadFromString(s, R"(
nodes:
  - id: bat1
    type: source
    subtype: battery
    nominal_voltage: 24.0
    battery: { capacity_ah: 35, initial_soc: 100 }
  - id: bat1
    type: bus
    label: "Duplicate"
connections: []
)");
        CHECK(!ok);
    }
    PASS();

    // T5 ── Connection references nonexistent node → load fails ──────
    TEST("Connection references nonexistent node → returns false");
    {
        GraphSolver s;
        bool ok = loadFromString(s, R"(
nodes:
  - id: bat1
    type: source
    subtype: battery
    nominal_voltage: 24.0
    battery: { capacity_ah: 35, initial_soc: 100 }
connections:
  - id: sw1
    type: switch
    from: bat1
    to: nonexistent_bus
)");
        CHECK(!ok);
    }
    PASS();

    // T6 ── Relay coil_bus references nonexistent bus → load fails ───
    TEST("Relay coil_bus references nonexistent bus → returns false");
    {
        GraphSolver s;
        bool ok = loadFromString(s, R"(
nodes:
  - id: bat1
    type: source
    subtype: battery
    nominal_voltage: 24.0
    battery: { capacity_ah: 35, initial_soc: 100 }
  - id: bus1
    type: bus
  - id: bus2
    type: bus
connections:
  - id: relay1
    type: relay
    from: bus1
    to: bus2
    coil_bus: ghost_bus
)");
        CHECK(!ok);
    }
    PASS();

    // T7 ── Unknown source subtype → load fails ─────────────────────
    TEST("Unknown source subtype → returns false");
    {
        GraphSolver s;
        bool ok = loadFromString(s, R"(
nodes:
  - id: src1
    type: source
    subtype: nuclear_reactor
    nominal_voltage: 28.0
connections: []
)");
        CHECK(!ok);
    }
    PASS();

    // T8 ── Unknown connection type → load fails ─────────────────────
    TEST("Unknown connection type → returns false");
    {
        GraphSolver s;
        bool ok = loadFromString(s, R"(
nodes:
  - id: bat1
    type: source
    subtype: battery
    nominal_voltage: 24.0
    battery: { capacity_ah: 35, initial_soc: 100 }
  - id: bus1
    type: bus
connections:
  - id: c1
    type: magical_bridge
    from: bat1
    to: bus1
)");
        CHECK(!ok);
    }
    PASS();

    // T9 ── Dangling load → loads true + warning (not fatal) ────────
    TEST("Dangling load (no path from source) → returns true + warning");
    {
        GraphSolver s;
        bool ok = loadFromString(s, R"(
nodes:
  - id: bat1
    type: source
    subtype: battery
    nominal_voltage: 24.0
    battery: { capacity_ah: 35, initial_soc: 100 }
  - id: bus1
    type: bus
  - id: orphan_load
    type: load
    nominal_current: 1.0
connections:
  - id: w1
    type: wire
    from: bat1
    to: bus1
)");
        CHECK(ok);  // warning only, not fatal
        CHECK(!s.getNodeStates().at("orphan_load").powered);
    }
    PASS();

    // ── Summary ─────────────────────────────────────────────────────
    std::cout << "\n" << tests_passed << "/" << tests_total
              << " tests passed." << std::endl;
    return (tests_passed == tests_total) ? 0 : 1;
}
