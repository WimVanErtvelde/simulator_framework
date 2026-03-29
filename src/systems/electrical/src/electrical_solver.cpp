#include "electrical/electrical_solver.hpp"
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <iostream>

// Minimal JSON parsing via nlohmann/json (header-only, included via CMake)
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <yaml-cpp/yaml.h>

namespace elec_sys {

// ─── JSON Topology Loader ────────────────────────────────────────

static GcuParams parseGcu(const json& j) {
    GcuParams g;
    if (j.contains("overvolt_threshold")) g.overvolt_threshold = j["overvolt_threshold"];
    if (j.contains("undervolt_threshold")) g.undervolt_threshold = j["undervolt_threshold"];
    if (j.contains("overfreq_threshold")) g.overfreq_threshold = j["overfreq_threshold"];
    if (j.contains("underfreq_threshold")) g.underfreq_threshold = j["underfreq_threshold"];
    if (j.contains("trip_delay_ms")) g.trip_delay_ms = j["trip_delay_ms"];
    if (j.contains("reset_mode")) g.reset_mode = j["reset_mode"];
    return g;
}

static BatteryParams parseBattery(const json& j) {
    BatteryParams b;
    if (j.contains("capacity_ah")) b.capacity_ah = j["capacity_ah"];
    if (j.contains("internal_resistance_ohm")) b.internal_resistance_ohm = j["internal_resistance_ohm"];
    if (j.contains("charge_rate_max")) b.charge_rate_max = j["charge_rate_max"];
    if (j.contains("initial_soc")) b.initial_soc = j["initial_soc"];
    if (j.contains("soc_voltage_curve")) {
        for (auto& pt : j["soc_voltage_curve"]) {
            b.soc_voltage_curve.emplace_back(pt[0].get<double>(), pt[1].get<double>());
        }
    }
    return b;
}

static CircuitBreakerDef parseCB(const json& j) {
    CircuitBreakerDef cb;
    if (j.contains("id")) cb.id = j["id"];
    if (j.contains("rating")) cb.rating = j["rating"];
    if (j.contains("trip_curve")) cb.trip_curve = j["trip_curve"];
    if (j.contains("panel_location")) cb.panel_location = j["panel_location"];
    return cb;
}

ElectricalSolver::ElectricalSolver() {}
ElectricalSolver::~ElectricalSolver() {}

bool ElectricalSolver::loadTopology(const std::string& json_path) {
    std::ifstream ifs(json_path);
    if (!ifs.is_open()) {
        std::cerr << "[ElecSys] Failed to open topology: " << json_path << std::endl;
        return false;
    }

    json root;
    try {
        ifs >> root;
    } catch (const json::parse_error& e) {
        std::cerr << "[ElecSys] JSON parse error: " << e.what() << std::endl;
        return false;
    }

    // Aircraft info
    if (root.contains("aircraft")) {
        auto& a = root["aircraft"];
        topology_.aircraft_type = a.value("type", "Unknown");
        topology_.designation = a.value("designation", "");
        topology_.revision = a.value("revision", "");
    }

    // Solver params
    if (root.contains("solver")) {
        auto& s = root["solver"];
        topology_.solver_rate_hz = s.value("rate_hz", 50.0);
        topology_.propagation_passes = s.value("propagation_passes", 4);
        topology_.contactor_delay_ms = s.value("contactor_delay_ms", 80.0);
    }

    // Sources
    for (auto& j : root["sources"]) {
        SourceDef sd;
        sd.id = j["id"];
        sd.type = j["type"];
        sd.label = j["label"];
        sd.nominal_voltage = j.value("nominal_voltage", 28.0);
        sd.nominal_frequency = j.value("nominal_frequency", 0.0);
        sd.engine_index = j.value("engine_index", -1);
        sd.min_rpm_pct = j.value("min_rpm_pct", 0.0);
        sd.max_current = j.value("max_current", 200.0);
        if (j.contains("gcu")) sd.gcu = parseGcu(j["gcu"]);
        if (j.contains("battery")) sd.battery = parseBattery(j["battery"]);
        topology_.sources.push_back(std::move(sd));
    }

    // Buses
    for (auto& j : root["buses"]) {
        BusDef bd;
        bd.id = j["id"];
        bd.label = j["label"];
        bd.type = j.value("type", "dc");
        bd.nominal_voltage = j.value("nominal_voltage", 28.0);
        bd.nominal_frequency = j.value("nominal_frequency", 0.0);
        bd.shed_priority = j.value("shed_priority", 0);
        topology_.buses.push_back(std::move(bd));
    }

    // Switches
    for (auto& j : root["switches"]) {
        SwitchDef sw;
        sw.id = j["id"];
        sw.type = j["type"];
        sw.label = j["label"];
        sw.source = j["source"];
        sw.target = j["target"];
        sw.default_closed = j.value("default_closed", false);
        sw.coil_bus = j.value("coil_bus", "");
        sw.switching_delay_ms = j.value("switching_delay_ms", topology_.contactor_delay_ms);
        sw.pilot_controllable = j.value("pilot_controllable", true);
        sw.current_rating = j.value("current_rating", 0.0);
        topology_.switches.push_back(std::move(sw));
    }

    // Loads
    for (auto& j : root["loads"]) {
        LoadDef ld;
        ld.id = j["id"];
        ld.label = j["label"];
        ld.bus = j["bus"];
        ld.type = j.value("type", "constant_current");
        ld.nominal_current = j.value("nominal_current", 1.0);
        ld.inrush_current = j.value("inrush_current", 0.0);
        ld.inrush_duration_ms = j.value("inrush_duration_ms", 0.0);
        if (j.contains("circuit_breaker")) ld.cb = parseCB(j["circuit_breaker"]);
        ld.essential = j.value("essential", false);
        if (j.contains("switch_id")) ld.switch_id = j["switch_id"].get<std::string>();
        if (j.contains("affected_systems")) {
            for (auto& s : j["affected_systems"]) {
                ld.affected_systems.push_back(s.get<std::string>());
            }
        }
        topology_.loads.push_back(std::move(ld));
    }

    // Direct connections
    if (root.contains("direct_connections")) {
        for (auto& j : root["direct_connections"]) {
            DirectConnection dc;
            dc.from = j["from"];
            dc.to = j["to"];
            topology_.direct_connections.push_back(std::move(dc));
        }
    }

    // CAS messages
    if (root.contains("cas_messages")) {
        for (auto& j : root["cas_messages"]) {
            CasMessageDef cm;
            cm.id = j["id"];
            cm.text = j["text"];
            cm.level = j["level"];
            if (j.contains("condition")) {
                auto& c = j["condition"];
                cm.condition_type = c.value("type", "");
                cm.target_id = c.value("target_id", "");
                cm.threshold = c.value("threshold", 0.0);
            }
            topology_.cas_messages.push_back(std::move(cm));
        }
    }

    std::cout << "[ElecSys] Loaded topology: " << topology_.aircraft_type
              << " | " << topology_.sources.size() << " sources, "
              << topology_.buses.size() << " buses, "
              << topology_.switches.size() << " switches, "
              << topology_.loads.size() << " loads" << std::endl;

    reset();
    return true;
}

void ElectricalSolver::loadTopology(const Topology& topo) {
    topology_ = topo;

    std::cout << "[ElecSys] Loaded topology: " << topology_.aircraft_type
              << " | " << topology_.sources.size() << " sources, "
              << topology_.buses.size() << " buses, "
              << topology_.switches.size() << " switches, "
              << topology_.loads.size() << " loads" << std::endl;

    reset();
}

// ─── YAML Topology Loader ───────────────────────────────────────

static GcuParams parseGcuYaml(const YAML::Node& n) {
    GcuParams g;
    if (n["overvolt_threshold"])  g.overvolt_threshold  = n["overvolt_threshold"].as<double>();
    if (n["undervolt_threshold"]) g.undervolt_threshold = n["undervolt_threshold"].as<double>();
    if (n["overfreq_threshold"])  g.overfreq_threshold  = n["overfreq_threshold"].as<double>();
    if (n["underfreq_threshold"]) g.underfreq_threshold = n["underfreq_threshold"].as<double>();
    if (n["trip_delay_ms"])       g.trip_delay_ms       = n["trip_delay_ms"].as<double>();
    if (n["reset_mode"])          g.reset_mode          = n["reset_mode"].as<std::string>();
    return g;
}

static BatteryParams parseBatteryYaml(const YAML::Node& n) {
    BatteryParams b;
    if (n["capacity_ah"])             b.capacity_ah             = n["capacity_ah"].as<double>();
    if (n["internal_resistance_ohm"]) b.internal_resistance_ohm = n["internal_resistance_ohm"].as<double>();
    if (n["charge_rate_max"])         b.charge_rate_max         = n["charge_rate_max"].as<double>();
    if (n["initial_soc"])            b.initial_soc             = n["initial_soc"].as<double>();
    if (n["soc_voltage_curve"]) {
        for (const auto& pt : n["soc_voltage_curve"]) {
            b.soc_voltage_curve.emplace_back(pt[0].as<double>(), pt[1].as<double>());
        }
    }
    return b;
}

static CircuitBreakerDef parseCBYaml(const YAML::Node& n) {
    CircuitBreakerDef cb;
    if (n["id"])             cb.id             = n["id"].as<std::string>();
    if (n["rating"])         cb.rating         = n["rating"].as<double>();
    if (n["trip_curve"])     cb.trip_curve     = n["trip_curve"].as<std::string>();
    if (n["panel_location"]) cb.panel_location = n["panel_location"].as<std::string>();
    return cb;
}

bool ElectricalSolver::loadTopologyYaml(const std::string& yaml_path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(yaml_path);
    } catch (const YAML::Exception& e) {
        std::cerr << "[ElecSys] YAML parse error: " << e.what() << std::endl;
        return false;
    }

    // Aircraft info
    if (root["aircraft"]) {
        auto a = root["aircraft"];
        if (a["type"])        topology_.aircraft_type = a["type"].as<std::string>();
        if (a["designation"]) topology_.designation   = a["designation"].as<std::string>();
        if (a["revision"])    topology_.revision      = a["revision"].as<std::string>();
    }

    // Solver params
    if (root["solver"]) {
        auto s = root["solver"];
        if (s["rate_hz"])            topology_.solver_rate_hz     = s["rate_hz"].as<double>();
        if (s["propagation_passes"]) topology_.propagation_passes = s["propagation_passes"].as<int>();
        if (s["contactor_delay_ms"]) topology_.contactor_delay_ms = s["contactor_delay_ms"].as<double>();
    }

    // Sources
    if (root["sources"]) {
        for (const auto& j : root["sources"]) {
            SourceDef sd;
            sd.id    = j["id"].as<std::string>();
            sd.type  = j["type"].as<std::string>();
            sd.label = j["label"].as<std::string>();
            if (j["nominal_voltage"])   sd.nominal_voltage  = j["nominal_voltage"].as<double>();
            if (j["nominal_frequency"]) sd.nominal_frequency = j["nominal_frequency"].as<double>();
            if (j["engine_index"])      sd.engine_index     = j["engine_index"].as<int>();
            if (j["min_rpm_pct"])       sd.min_rpm_pct      = j["min_rpm_pct"].as<double>();
            if (j["max_current"])       sd.max_current      = j["max_current"].as<double>();
            if (j["gcu"])               sd.gcu              = parseGcuYaml(j["gcu"]);
            if (j["battery"])           sd.battery          = parseBatteryYaml(j["battery"]);
            topology_.sources.push_back(std::move(sd));
        }
    }

    // Buses
    if (root["buses"]) {
        for (const auto& j : root["buses"]) {
            BusDef bd;
            bd.id    = j["id"].as<std::string>();
            bd.label = j["label"].as<std::string>();
            if (j["type"])              bd.type              = j["type"].as<std::string>();
            if (j["nominal_voltage"])   bd.nominal_voltage   = j["nominal_voltage"].as<double>();
            if (j["nominal_frequency"]) bd.nominal_frequency = j["nominal_frequency"].as<double>();
            if (j["shed_priority"])     bd.shed_priority     = j["shed_priority"].as<int>();
            topology_.buses.push_back(std::move(bd));
        }
    }

    // Switches
    if (root["switches"]) {
        for (const auto& j : root["switches"]) {
            SwitchDef sw;
            sw.id     = j["id"].as<std::string>();
            sw.type   = j["type"].as<std::string>();
            sw.label  = j["label"].as<std::string>();
            sw.source = j["source"].as<std::string>();
            sw.target = j["target"].as<std::string>();
            if (j["default_closed"])     sw.default_closed    = j["default_closed"].as<bool>();
            if (j["coil_bus"])           sw.coil_bus          = j["coil_bus"].as<std::string>();
            if (j["switching_delay_ms"]) sw.switching_delay_ms = j["switching_delay_ms"].as<double>();
            if (j["pilot_controllable"]) sw.pilot_controllable = j["pilot_controllable"].as<bool>();
            if (j["current_rating"])     sw.current_rating    = j["current_rating"].as<double>();
            topology_.switches.push_back(std::move(sw));
        }
    }

    // Loads
    if (root["loads"]) {
        for (const auto& j : root["loads"]) {
            LoadDef ld;
            ld.id    = j["id"].as<std::string>();
            ld.label = j["label"].as<std::string>();
            ld.bus   = j["bus"].as<std::string>();
            if (j["type"])               ld.type               = j["type"].as<std::string>();
            if (j["nominal_current"])    ld.nominal_current    = j["nominal_current"].as<double>();
            if (j["inrush_current"])     ld.inrush_current     = j["inrush_current"].as<double>();
            if (j["inrush_duration_ms"]) ld.inrush_duration_ms = j["inrush_duration_ms"].as<double>();
            if (j["circuit_breaker"])    ld.cb                 = parseCBYaml(j["circuit_breaker"]);
            if (j["essential"])          ld.essential          = j["essential"].as<bool>();
            if (j["switch_id"])          ld.switch_id          = j["switch_id"].as<std::string>();
            if (j["affected_systems"]) {
                for (const auto& s : j["affected_systems"]) {
                    ld.affected_systems.push_back(s.as<std::string>());
                }
            }
            topology_.loads.push_back(std::move(ld));
        }
    }

    // Direct connections
    if (root["direct_connections"]) {
        for (const auto& j : root["direct_connections"]) {
            DirectConnection dc;
            dc.from = j["from"].as<std::string>();
            dc.to   = j["to"].as<std::string>();
            topology_.direct_connections.push_back(std::move(dc));
        }
    }

    // CAS messages
    if (root["cas_messages"]) {
        for (const auto& j : root["cas_messages"]) {
            CasMessageDef cm;
            cm.id    = j["id"].as<std::string>();
            cm.text  = j["text"].as<std::string>();
            cm.level = j["level"].as<std::string>();
            if (j["condition"]) {
                auto c = j["condition"];
                if (c["type"])      cm.condition_type = c["type"].as<std::string>();
                if (c["target_id"]) cm.target_id      = c["target_id"].as<std::string>();
                if (c["threshold"]) cm.threshold      = c["threshold"].as<double>();
            }
            topology_.cas_messages.push_back(std::move(cm));
        }
    }

    std::cout << "[ElecSys] Loaded YAML topology: " << topology_.aircraft_type
              << " | " << topology_.sources.size() << " sources, "
              << topology_.buses.size() << " buses, "
              << topology_.switches.size() << " switches, "
              << topology_.loads.size() << " loads" << std::endl;

    reset();
    return true;
}

// ─── Reset ───────────────────────────────────────────────────────

void ElectricalSolver::reset() {
    source_states_.clear();
    bus_states_.clear();
    switch_states_.clear();
    load_states_.clear();
    faults_.clear();
    panel_switch_states_.clear();
    active_cas_.clear();
    sim_time_ = 0.0;

    for (auto& sd : topology_.sources) {
        SourceState ss;
        if (sd.battery.has_value()) {
            ss.battery_soc = sd.battery->initial_soc;
        } else {
            ss.battery_soc = -1.0;
        }
        source_states_[sd.id] = ss;
    }

    for (auto& bd : topology_.buses) {
        bus_states_[bd.id] = BusState{};
    }

    for (auto& sw : topology_.switches) {
        SwitchState ss;
        ss.closed = sw.default_closed;
        ss.commanded_position = sw.default_closed;
        switch_states_[sw.id] = ss;
    }

    for (auto& ld : topology_.loads) {
        LoadState ls;
        ls.cb_closed = true;
        load_states_[ld.id] = ls;
    }

    // Size engine inputs
    int max_eng = 0;
    for (auto& sd : topology_.sources) {
        if (sd.engine_index >= 0) max_eng = std::max(max_eng, sd.engine_index + 1);
    }
    fdm_inputs_.engine_n2_pct.resize(max_eng, 0.0);
}

// ─── Main Solver Step ────────────────────────────────────────────

void ElectricalSolver::step(double dt) {
    updateSources(dt);
    resetBuses();
    propagateDirectConnections();

    for (int i = 0; i < topology_.propagation_passes; ++i) {
        propagatePowerFlow();
        updateRelayCoils();   // after propagation so coil buses are energised
    }

    updateLoads(dt);
    updateBatterySoc(dt);
    evaluateCas();

    sim_time_ += dt;
}

// ─── Source Models ───────────────────────────────────────────────

void ElectricalSolver::updateSources(double dt) {
    for (auto& sd : topology_.sources) {
        auto& ss = source_states_[sd.id];
        auto fault_it = faults_.find(sd.id);
        std::string fault = (fault_it != faults_.end()) ? fault_it->second : "";

        if (fault == "fail") {
            ss.online = false;
            ss.voltage = 0.0;
            ss.current = 0.0;
            ss.frequency = 0.0;
            continue;
        }

        if (sd.type == "dc_generator" || sd.type == "ac_generator" || sd.type == "starter_generator") {
            double n2 = 0.0;
            if (sd.engine_index >= 0 && sd.engine_index < (int)fdm_inputs_.engine_n2_pct.size()) {
                n2 = fdm_inputs_.engine_n2_pct[sd.engine_index];
            }

            bool rpm_ok = (n2 >= sd.min_rpm_pct);

            // GCU protection logic
            if (sd.gcu.has_value() && !ss.gcu_tripped) {
                double v = ss.voltage;
                bool limit_exceeded = false;
                if (v > 0 && v > sd.gcu->overvolt_threshold) limit_exceeded = true;
                if (v > 0 && v < sd.gcu->undervolt_threshold && rpm_ok) limit_exceeded = true;

                if (fault == "overvolt" || fault == "undervolt") limit_exceeded = true;

                if (limit_exceeded) {
                    ss.gcu_trip_timer += dt * 1000.0;
                    if (ss.gcu_trip_timer >= sd.gcu->trip_delay_ms) {
                        ss.gcu_tripped = true;
                    }
                } else {
                    ss.gcu_trip_timer = 0.0;
                }
            }

            ss.online = rpm_ok && !ss.gcu_tripped;
            if (ss.online) {
                double rpm_factor = std::min(1.0, (n2 - sd.min_rpm_pct) / (100.0 - sd.min_rpm_pct));
                ss.voltage = sd.nominal_voltage * (0.95 + 0.05 * rpm_factor);
                if (fault == "overvolt") ss.voltage = sd.gcu.has_value() ? sd.gcu->overvolt_threshold + 2.0 : 34.0;
                if (fault == "undervolt") ss.voltage = sd.gcu.has_value() ? sd.gcu->undervolt_threshold - 2.0 : 16.0;
                if (sd.nominal_frequency > 0) {
                    ss.frequency = sd.nominal_frequency * (n2 / 100.0);
                }
            } else {
                ss.voltage = 0.0;
                ss.frequency = 0.0;
            }

        } else if (sd.type == "apu_generator") {
            ss.online = fdm_inputs_.apu_running;
            ss.voltage = ss.online ? sd.nominal_voltage : 0.0;
            if (sd.nominal_frequency > 0) ss.frequency = ss.online ? sd.nominal_frequency : 0.0;

        } else if (sd.type == "external_power") {
            ss.online = fdm_inputs_.external_power_connected;
            ss.voltage = ss.online ? sd.nominal_voltage : 0.0;

        } else if (sd.type == "battery") {
            if (fault == "dead") {
                ss.online = false;
                ss.voltage = 0.0;
                ss.battery_soc = 0.0;
            } else {
                ss.online = true;
                if (sd.battery.has_value()) {
                    double ocv = interpolateSocVoltage(sd.battery.value(), ss.battery_soc);
                    // Voltage sag under load: V = OCV - I*R_internal
                    double load_current = ss.current; // from previous frame
                    ss.voltage = ocv - load_current * sd.battery->internal_resistance_ohm;
                    ss.voltage = std::max(0.0, ss.voltage);
                } else {
                    ss.voltage = sd.nominal_voltage * (0.8 + 0.2 * (ss.battery_soc / 100.0));
                }
            }
        }
    }
}

double ElectricalSolver::interpolateSocVoltage(const BatteryParams& bp, double soc) const {
    if (bp.soc_voltage_curve.empty()) {
        return 24.0 * (0.8 + 0.2 * (soc / 100.0)); // fallback linear
    }

    soc = std::clamp(soc, 0.0, 100.0);
    auto& curve = bp.soc_voltage_curve;

    if (soc <= curve.front().first) return curve.front().second;
    if (soc >= curve.back().first) return curve.back().second;

    for (size_t i = 0; i < curve.size() - 1; ++i) {
        if (soc >= curve[i].first && soc <= curve[i + 1].first) {
            double t = (soc - curve[i].first) / (curve[i + 1].first - curve[i].first);
            return curve[i].second + t * (curve[i + 1].second - curve[i].second);
        }
    }
    return curve.back().second;
}

// ─── Bus Reset ───────────────────────────────────────────────────

void ElectricalSolver::resetBuses() {
    for (auto& bd : topology_.buses) {
        auto& bs = bus_states_[bd.id];
        bs.powered = false;
        bs.voltage = 0.0;
        bs.current = 0.0;
        bs.source_id.clear();
    }
}

// ─── Direct Connections ──────────────────────────────────────────

void ElectricalSolver::propagateDirectConnections() {
    for (auto& dc : topology_.direct_connections) {
        auto src_it = source_states_.find(dc.from);
        auto bus_it = bus_states_.find(dc.to);
        if (src_it != source_states_.end() && bus_it != bus_states_.end()) {
            auto& ss = src_it->second;
            auto& bs = bus_it->second;
            if (ss.online && ss.voltage > bs.voltage) {
                bs.voltage = ss.voltage;
                bs.powered = true;
                bs.source_id = dc.from;
            }
        }
    }
}

// ─── Relay Coil Logic ────────────────────────────────────────────

void ElectricalSolver::updateRelayCoils() {
    for (auto& sw : topology_.switches) {
        if (sw.type == "relay" && !sw.coil_bus.empty()) {
            auto& ss = switch_states_[sw.id];
            auto fault_it = faults_.find(sw.id);
            std::string fault = (fault_it != faults_.end()) ? fault_it->second : "";

            if (fault == "stuck_open") {
                ss.closed = false;
                ss.tripped = true;
                continue;
            }
            if (fault == "welded") {
                ss.closed = true;
                continue;
            }

            // Relay requires coil power — coil state overrides commanded position
            auto coil_it = bus_states_.find(sw.coil_bus);
            if (coil_it != bus_states_.end()) {
                ss.closed = coil_it->second.powered; // energized = closed, de-energized = open
            }
        }
    }
}

// ─── Power Flow Propagation ──────────────────────────────────────

void ElectricalSolver::propagatePowerFlow() {
    for (auto& sw : topology_.switches) {
        auto& ss = switch_states_[sw.id];
        auto fault_it = faults_.find(sw.id);
        std::string fault = (fault_it != faults_.end()) ? fault_it->second : "";

        if (fault == "stuck_open") { ss.closed = false; ss.tripped = true; }
        if (fault == "welded") { ss.closed = true; }

        if (!ss.closed || ss.tripped) continue;

        double src_voltage = 0.0;
        std::string src_id;

        // Source can be a power source or another bus
        auto src_source = source_states_.find(sw.source);
        if (src_source != source_states_.end()) {
            if (src_source->second.online) {
                src_voltage = src_source->second.voltage;
                src_id = sw.source;
            }
        } else {
            auto src_bus = bus_states_.find(sw.source);
            if (src_bus != bus_states_.end() && src_bus->second.powered) {
                src_voltage = src_bus->second.voltage;
                src_id = src_bus->second.source_id;
            }
        }

        if (src_voltage > 0.0) {
            auto tgt_it = bus_states_.find(sw.target);
            if (tgt_it != bus_states_.end()) {
                auto& tgt = tgt_it->second;
                if (src_voltage > tgt.voltage) {
                    tgt.voltage = src_voltage;
                    tgt.powered = true;
                    tgt.source_id = src_id;
                }
            }
        }
    }
}

// ─── Load Updates ────────────────────────────────────────────────

void ElectricalSolver::updateLoads(double dt) {
    for (auto& ld : topology_.loads) {
        auto& ls = load_states_[ld.id];
        auto& bus = bus_states_[ld.bus];
        auto fault_it = faults_.find(ld.id);
        std::string fault = (fault_it != faults_.end()) ? fault_it->second : "";

        // Short circuit trips the CB
        if (fault == "short") {
            ls.cb_tripped = true;
        }

        bool cb_ok = ls.cb_closed && !ls.cb_tripped && !ls.cb_pulled;

        // Load switch gating: if switch_id is set, load only draws when that switch is ON
        bool switch_on = true;
        if (!ld.switch_id.empty()) {
            auto ps_it = panel_switch_states_.find(ld.switch_id);
            switch_on = (ps_it != panel_switch_states_.end()) ? ps_it->second : false;
        }

        ls.powered = bus.powered && cb_ok && switch_on && fault != "open";

        if (ld.id == "com1" || ld.id == "com2") {
            std::cout << ld.id << ": bus=" << ld.bus
                      << " bus_powered=" << bus.powered
                      << " bus_v=" << bus.voltage
                      << " cb_closed=" << ls.cb_closed
                      << " switch_id='" << ld.switch_id << "'"
                      << " sw_on=" << (ld.switch_id.empty() ? true :
                         (panel_switch_states_.count(ld.switch_id) ?
                          panel_switch_states_[ld.switch_id] : false))
                      << " current=" << ls.current
                      << std::endl;
        }

        if (ls.powered) {
            double draw = ld.nominal_current;

            // Motor inrush
            if (ld.type == "motor" && ld.inrush_current > 0.0 && ls.inrush_timer > 0.0) {
                double inrush_frac = ls.inrush_timer / (ld.inrush_duration_ms / 1000.0);
                draw = ld.nominal_current + (ld.inrush_current - ld.nominal_current) * inrush_frac;
                ls.inrush_timer -= dt;
                if (ls.inrush_timer < 0.0) ls.inrush_timer = 0.0;
            }

            // Resistive loads: current varies with voltage
            if (ld.type == "resistive") {
                // At nominal voltage, draws nominal current. Resistance = V_nom / I_nom
                auto bus_def_it = std::find_if(topology_.buses.begin(), topology_.buses.end(),
                    [&](const BusDef& b) { return b.id == ld.bus; });
                if (bus_def_it != topology_.buses.end() && bus_def_it->nominal_voltage > 0) {
                    double R = bus_def_it->nominal_voltage / ld.nominal_current;
                    draw = bus.voltage / R;
                }
            }

            // CB overcurrent check
            if (ld.cb.rating > 0.0 && draw > ld.cb.rating * 1.3) {
                // Simplified: instant trip at 130% of rating
                ls.cb_tripped = true;
                ls.powered = false;
                draw = 0.0;
            }

            ls.current = draw;
            bus.current += draw;
        } else {
            ls.current = 0.0;
            // If load just became powered, start inrush timer
        }
    }
}

// ─── Battery SOC ─────────────────────────────────────────────────

void ElectricalSolver::updateBatterySoc(double dt) {
    for (auto& sd : topology_.sources) {
        if (!sd.battery.has_value()) continue;
        auto& ss = source_states_[sd.id];
        if (ss.battery_soc < 0) continue;

        // Calculate total current drawn from this battery
        double total_current = 0.0;
        for (auto& bd : topology_.buses) {
            auto& bs = bus_states_[bd.id];
            if (bs.powered && bs.source_id == sd.id) {
                total_current += bs.current;
            }
        }
        ss.current = total_current;

        if (total_current > 0.0) {
            // Discharge
            double soc_drain = (total_current * dt) / (sd.battery->capacity_ah * 3600.0) * 100.0;
            ss.battery_soc = std::max(0.0, ss.battery_soc - soc_drain);
        } else {
            // Check if battery can charge from a powered bus through its switch
            for (auto& sw : topology_.switches) {
                if (sw.source == sd.id) {
                    auto& sws = switch_states_[sw.id];
                    if (sws.closed && !sws.tripped) {
                        auto bus_it = bus_states_.find(sw.target);
                        if (bus_it != bus_states_.end() && bus_it->second.powered
                            && bus_it->second.source_id != sd.id) {
                            double charge_rate = std::min(sd.battery->charge_rate_max,
                                (100.0 - ss.battery_soc) * 0.5); // taper
                            double soc_gain = (charge_rate * dt) / (sd.battery->capacity_ah * 3600.0) * 100.0;
                            ss.battery_soc = std::min(100.0, ss.battery_soc + soc_gain);
                        }
                    }
                }
            }
        }
    }
}

// ─── CAS Evaluation ──────────────────────────────────────────────

void ElectricalSolver::evaluateCas() {
    active_cas_.clear();

    for (auto& cm : topology_.cas_messages) {
        bool triggered = false;
        int level = 0;
        if (cm.level == "advisory") level = 0;
        else if (cm.level == "caution") level = 1;
        else if (cm.level == "warning") level = 2;

        if (cm.condition_type == "bus_dead") {
            auto it = bus_states_.find(cm.target_id);
            if (it != bus_states_.end() && !it->second.powered) triggered = true;

        } else if (cm.condition_type == "source_fail") {
            auto it = source_states_.find(cm.target_id);
            if (it != source_states_.end()) {
                // Only trigger if the source SHOULD be online but isn't
                auto fault_it = faults_.find(cm.target_id);
                if (fault_it != faults_.end() && !it->second.online) triggered = true;
                if (it->second.gcu_tripped) triggered = true;
            }

        } else if (cm.condition_type == "low_voltage") {
            auto it = bus_states_.find(cm.target_id);
            if (it != bus_states_.end() && it->second.powered && it->second.voltage < cm.threshold) {
                triggered = true;
            }

        } else if (cm.condition_type == "low_soc") {
            auto it = source_states_.find(cm.target_id);
            if (it != source_states_.end() && it->second.battery_soc >= 0 && it->second.battery_soc < cm.threshold) {
                triggered = true;
            }

        } else if (cm.condition_type == "cb_trip") {
            auto it = load_states_.find(cm.target_id);
            if (it != load_states_.end() && it->second.cb_tripped) triggered = true;
        }

        if (triggered) {
            active_cas_.push_back({cm.text, level});
        }
    }

    // Sort: warnings first, then cautions, then advisories
    std::sort(active_cas_.begin(), active_cas_.end(),
        [](const CasMessage& a, const CasMessage& b) { return a.level > b.level; });
}

// ─── Commands ────────────────────────────────────────────────────

void ElectricalSolver::setFdmInputs(const FdmInputs& inputs) {
    fdm_inputs_ = inputs;
    // Ensure engine vector is sized correctly
    int max_eng = 0;
    for (auto& sd : topology_.sources) {
        if (sd.engine_index >= 0) max_eng = std::max(max_eng, sd.engine_index + 1);
    }
    fdm_inputs_.engine_n2_pct.resize(max_eng, 0.0);
}

void ElectricalSolver::commandSwitch(const std::string& id, int cmd) {
    // Store panel switch state for load gating (sw_landing_lt, sw_pitot_heat, etc.)
    switch (cmd) {
        case 0: panel_switch_states_[id] = false; break;
        case 1: panel_switch_states_[id] = true; break;
        case 2: {
            auto ps_it = panel_switch_states_.find(id);
            bool prev = (ps_it != panel_switch_states_.end()) ? ps_it->second : false;
            panel_switch_states_[id] = !prev;
            break;
        }
    }

    // Topology switches (source→bus connections)
    auto it = switch_states_.find(id);
    if (it == switch_states_.end()) return;

    // Relay-type switches with a coil_bus are driven by coil logic, not commands
    for (const auto& sw : topology_.switches) {
        if (sw.id == id && sw.type == "relay" && !sw.coil_bus.empty()) return;
    }

    auto& ss = it->second;
    switch (cmd) {
        case 0: ss.commanded_position = false; ss.closed = false; ss.tripped = false; break;
        case 1: ss.commanded_position = true; ss.closed = true; ss.tripped = false; break;
        case 2: ss.commanded_position = !ss.commanded_position; ss.closed = ss.commanded_position; ss.tripped = false; break;
    }
}

void ElectricalSolver::commandCircuitBreaker(const std::string& id, int cmd) {
    for (auto& ld : topology_.loads) {
        if (ld.cb.id == id) {
            auto& ls = load_states_[ld.id];
            switch (cmd) {
                case 0: ls.cb_pulled = true; ls.cb_closed = false; break;  // pull
                case 1: ls.cb_closed = true; ls.cb_pulled = false; ls.cb_tripped = false; break; // reset
                case 2: // toggle
                    if (ls.cb_tripped) { ls.cb_tripped = false; ls.cb_closed = true; ls.cb_pulled = false; }
                    else { ls.cb_pulled = !ls.cb_pulled; ls.cb_closed = !ls.cb_pulled; }
                    break;
            }
            // Start inrush if motor load just got power
            if (ls.cb_closed && !ls.cb_tripped && ld.type == "motor" && ld.inrush_duration_ms > 0) {
                ls.inrush_timer = ld.inrush_duration_ms / 1000.0;
            }
            return;
        }
    }
}

void ElectricalSolver::injectFault(const std::string& target_id, const std::string& fault_type) {
    faults_[target_id] = fault_type;
    std::cout << "[ElecSys] Fault injected: " << target_id << " -> " << fault_type << std::endl;
}

void ElectricalSolver::clearFault(const std::string& target_id) {
    faults_.erase(target_id);
    std::cout << "[ElecSys] Fault cleared: " << target_id << std::endl;
}

void ElectricalSolver::clearAllFaults() {
    faults_.clear();
    // Reset GCU trips
    for (auto& ss : source_states_) {
        ss.second.gcu_tripped = false;
        ss.second.gcu_trip_timer = 0.0;
    }
    std::cout << "[ElecSys] All faults cleared" << std::endl;
}

} // namespace elec_sys
