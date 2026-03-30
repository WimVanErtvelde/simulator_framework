#include "electrical/graph_solver.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

#ifndef TEST_YAML_PATH
#error "TEST_YAML_PATH must be defined at compile time"
#endif

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

static GraphSolver makeSolver() {
    GraphSolver s;
    bool ok = s.loadTopologyYaml(TEST_YAML_PATH);
    assert(ok && "Failed to load electrical_v2.yaml");
    return s;
}

static void powerBattery(GraphSolver& s) {
    s.commandConnection("sw_battery", 1);
}

static void powerAlternator(GraphSolver& s) {
    FdmInputs fdm;
    fdm.engine_n2_pct = {75.0};
    s.setFdmInputs(fdm);
    s.commandConnection("sw_alt", 1);
}

// ─── Tests ──────────────────────────────────────────────────────────

int main() {

    // T1 ── Load YAML, verify node/connection counts ─────────────────
    TEST("Load YAML, verify node/connection counts");
    {
        GraphSolver s = makeSolver();
        auto& topo = s.getTopology();
        // 3 sources + 4 buses + 11 junctions + 21 loads = 39 nodes
        CHECK(topo.nodes.size() == 39);
        CHECK(topo.connections.size() == 38);
    }
    PASS();

    // T2 ── All switches default (open) → main buses dead ────────────
    TEST("All switches open -> primary/avionics/essential buses dead");
    {
        GraphSolver s = makeSolver();
        s.step(0.02);
        auto& ns = s.getNodeStates();
        CHECK(!ns.at("primary_bus").powered);
        CHECK(!ns.at("avionics_bus").powered);
        CHECK(!ns.at("essential_bus").powered);
        // hot_batt_bus powered via wire from battery (always on)
        CHECK(ns.at("hot_batt_bus").powered);
        // Loads on unpowered buses are off
        CHECK(!ns.at("com1").powered);
        CHECK(!ns.at("nav2").powered);
        CHECK(!ns.at("fuel_pump").powered);
    }
    PASS();

    // T3 ── Close sw_battery → primary_bus at ~24V ───────────────────
    TEST("Close sw_battery -> primary_bus powered at ~24V");
    {
        GraphSolver s = makeSolver();
        powerBattery(s);
        s.step(0.02);
        auto& ns = s.getNodeStates();
        CHECK(ns.at("primary_bus").powered);
        CHECK_NEAR(ns.at("primary_bus").voltage, 24.0, 1.5);
    }
    PASS();

    // T4 ── Battery + alternator + engine → ~28V ─────────────────────
    TEST("Battery + alternator + engine -> primary_bus at ~28V");
    {
        GraphSolver s = makeSolver();
        powerBattery(s);
        powerAlternator(s);
        s.step(0.02);
        auto& ns = s.getNodeStates();
        CHECK(ns.at("primary_bus").powered);
        CHECK_NEAR(ns.at("primary_bus").voltage, 28.0, 1.5);
    }
    PASS();

    // T5 ── Avionics master → avionics_bus powered ───────────────────
    TEST("Close sw_avionics_master -> avionics_bus powered");
    {
        GraphSolver s = makeSolver();
        powerBattery(s);
        s.commandConnection("sw_avionics_master", 1);
        s.step(0.02);
        auto& ns = s.getNodeStates();
        CHECK(ns.at("avionics_bus").powered);
        CHECK_NEAR(ns.at("avionics_bus").voltage, ns.at("primary_bus").voltage, 0.01);
    }
    PASS();

    // T6 ── Essential bus powered via relay ───────────────────────────
    TEST("Essential bus powered via relay (coil_bus = primary_bus)");
    {
        GraphSolver s = makeSolver();
        powerBattery(s);
        s.step(0.02);
        auto& ns = s.getNodeStates();
        CHECK(ns.at("essential_bus").powered);
        CHECK_NEAR(ns.at("essential_bus").voltage, ns.at("primary_bus").voltage, 0.01);
    }
    PASS();

    // T7 ── Hot batt bus via wire (always on) ────────────────────────
    TEST("Hot batt bus powered via wire (always, when battery has SOC)");
    {
        GraphSolver s = makeSolver();
        s.step(0.02);
        auto& ns = s.getNodeStates();
        CHECK(ns.at("hot_batt_bus").powered);
        CHECK(ns.at("hot_batt_bus").voltage > 20.0);
        CHECK(ns.at("clock").powered);
        CHECK(ns.at("elt").powered);
    }
    PASS();

    // T8 ── CB trip via failure effect ──────────────────────────────
    TEST("Failure-driven CB trip -> com1 unpowered, clear -> restored");
    {
        GraphSolver s = makeSolver();
        powerBattery(s);
        s.commandConnection("sw_com1", 1);
        s.step(0.02);
        CHECK(s.getNodeStates().at("com1").powered);

        // Trip CB via failure effect
        s.applyFailureEffect("cb_com1", "set", "tripped", "true");
        s.step(0.02);
        CHECK(s.getConnectionStates().at("cb_com1").tripped);
        CHECK(!s.getNodeStates().at("com1").powered);

        // Clear failure → CB resets, com1 powered again
        s.clearFailureEffect("cb_com1", "tripped");
        s.step(0.02);
        CHECK(!s.getConnectionStates().at("cb_com1").tripped);
        CHECK(s.getNodeStates().at("com1").powered);
    }
    PASS();

    // T9 ── CB pull and reset ────────────────────────────────────────
    TEST("CB pull -> com1 unpowered. CB reset -> com1 powered.");
    {
        GraphSolver s = makeSolver();
        powerBattery(s);
        s.commandConnection("sw_com1", 1);
        s.step(0.02);
        CHECK(s.getNodeStates().at("com1").powered);

        s.commandConnection("cb_com1", 0); // pull
        s.step(0.02);
        CHECK(!s.getNodeStates().at("com1").powered);

        s.commandConnection("cb_com1", 1); // reset
        s.step(0.02);
        CHECK(s.getNodeStates().at("com1").powered);
    }
    PASS();

    // T10 ── Switch gate ─────────────────────────────────────────────
    TEST("Switch gate: sw_com1 open -> com1 unpowered even with CB closed");
    {
        GraphSolver s = makeSolver();
        powerBattery(s);
        s.step(0.02);
        CHECK(!s.getNodeStates().at("com1").powered); // sw_com1 default open

        s.commandConnection("sw_com1", 1);
        s.step(0.02);
        CHECK(s.getNodeStates().at("com1").powered);

        s.commandConnection("sw_com1", 0);
        s.step(0.02);
        CHECK(!s.getNodeStates().at("com1").powered);
    }
    PASS();

    // T11 ── Failure: alternator offline ─────────────────────────────
    TEST("Failure inject: force alternator offline -> battery only");
    {
        GraphSolver s = makeSolver();
        powerBattery(s);
        powerAlternator(s);
        s.step(0.02);
        CHECK_NEAR(s.getNodeStates().at("primary_bus").voltage, 28.0, 1.5);

        s.applyFailureEffect("alternator", "set", "online", "false");
        s.step(0.02);
        auto& ns = s.getNodeStates();
        CHECK(ns.at("primary_bus").powered);
        CHECK_NEAR(ns.at("primary_bus").voltage, 24.0, 1.5);
        CHECK(ns.at("primary_bus").power_source == "battery");
    }
    PASS();

    // T12 ── Failure: jam switch closed ──────────────────────────────
    TEST("Failure jam: jam sw_battery closed -> command open ignored");
    {
        GraphSolver s = makeSolver();
        powerBattery(s);
        s.step(0.02);
        CHECK(s.getNodeStates().at("primary_bus").powered);

        s.applyFailureEffect("sw_battery", "set", "jammed", "true");
        s.commandConnection("sw_battery", 0); // try to open
        s.step(0.02);
        CHECK(s.getNodeStates().at("primary_bus").powered); // still powered
    }
    PASS();

    // T13 ── CAS: ALT FAIL ──────────────────────────────────────────
    TEST("CAS: alternator offline -> ALT FAIL in CAS messages");
    {
        GraphSolver s = makeSolver();
        powerBattery(s);
        powerAlternator(s);
        s.step(0.02);

        s.applyFailureEffect("alternator", "set", "online", "false");
        s.step(0.02);

        bool found = false;
        for (auto& msg : s.getCasMessages()) {
            if (msg.text == "ALT FAIL") { found = true; break; }
        }
        CHECK(found);
    }
    PASS();

    // T14 ── Battery SOC drain (no spurious CB trips) ──────────────
    TEST("Battery SOC drains over time with loads, no CB trips");
    {
        GraphSolver s = makeSolver();
        powerBattery(s);
        s.commandConnection("sw_avionics_master", 1);
        s.commandConnection("sw_com1", 1);
        s.commandConnection("sw_landing_lt", 1);

        s.step(0.02);
        double soc_before = s.getNodeStates().at("battery").battery_soc;

        for (int i = 0; i < 100; ++i)
            s.step(0.02);

        double soc_after = s.getNodeStates().at("battery").battery_soc;
        CHECK(soc_after < soc_before);
        // No CBs should have tripped — overcurrent physics removed
        CHECK(!s.getConnectionStates().at("cb_flaps").tripped);
        CHECK(!s.getConnectionStates().at("cb_com1").tripped);
        CHECK(!s.getConnectionStates().at("cb_ldg_lt").tripped);
    }
    PASS();

    // T15 ── No auto-trip: CBs only trip from failures/commands ──────
    TEST("No auto-trip: high-current load does NOT trip CB");
    {
        GraphSolver s = makeSolver();
        powerBattery(s);
        // Starter: 150A nominal on an 80A CB — would have tripped with old model
        s.commandConnection("sw_starter_engage", 1);
        for (int i = 0; i < 50; ++i)
            s.step(0.02);
        CHECK(!s.getConnectionStates().at("cb_starter").tripped);
        CHECK(s.getNodeStates().at("starter").powered);
    }
    PASS();

    // ── Summary ─────────────────────────────────────────────────────
    std::cout << "\n" << tests_passed << "/" << tests_total
              << " tests passed." << std::endl;
    return (tests_passed == tests_total) ? 0 : 1;
}
