#include "fuel/fuel_graph_solver.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <queue>
#include <unordered_set>

namespace fuel_graph {

// ─── Helpers ────────────────────────────────────────────────────────

static NodeType parseNodeType(const std::string& s) {
    if (s == "tank")          return NodeType::tank;
    if (s == "pump")          return NodeType::pump;
    if (s == "junction")      return NodeType::junction;
    if (s == "engine_inlet")  return NodeType::engine_inlet;
    return NodeType::junction;
}

static const std::unordered_set<std::string> valid_node_types = {
    "tank", "pump", "junction", "engine_inlet"
};

static ConnectionType parseConnectionType(const std::string& s) {
    if (s == "line")        return ConnectionType::line;
    if (s == "valve")       return ConnectionType::valve;
    if (s == "selector")    return ConnectionType::selector;
    if (s == "check_valve") return ConnectionType::check_valve;
    return ConnectionType::line;
}

static const std::unordered_set<std::string> valid_conn_types = {
    "line", "valve", "selector", "check_valve"
};

static PumpSource parsePumpSource(const std::string& s) {
    if (s == "electrical") return PumpSource::electrical;
    return PumpSource::engine;
}

// ─── Constructor / Destructor ───────────────────────────────────────

FuelGraphSolver::FuelGraphSolver() {}
FuelGraphSolver::~FuelGraphSolver() {}

// ─── YAML v2 Loader ─────────────────────────────────────────────────

bool FuelGraphSolver::loadTopologyYaml(const std::string& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        std::cerr << "[FuelGraphSolver] YAML parse error: " << e.what() << std::endl;
        return false;
    }

    topology_ = Topology{};

    // Fuel info
    if (root["fuel"]) {
        auto f = root["fuel"];
        if (f["type"])               topology_.fuel_type            = f["type"].as<std::string>();
        if (f["density_kg_per_liter"]) topology_.density_kg_per_liter = f["density_kg_per_liter"].as<double>();
        if (f["low_fuel_threshold_norm"]) topology_.low_fuel_threshold_norm = f["low_fuel_threshold_norm"].as<double>();
    }
    if (root["aircraft"]) {
        auto a = root["aircraft"];
        if (a["type"]) topology_.aircraft_type = a["type"].as<std::string>();
    }

    // ── Nodes ───────────────────────────────────────────────────────
    if (root["nodes"]) {
        for (const auto& n : root["nodes"]) {
            Node node;
            node.id = n["id"].as<std::string>();
            std::string type_str = n["type"].as<std::string>();
            if (valid_node_types.find(type_str) == valid_node_types.end()) {
                std::cerr << "[FuelGraphSolver] VALIDATION ERROR: node '" << node.id
                          << "' has unknown type '" << type_str << "'" << std::endl;
                return false;
            }
            node.type = parseNodeType(type_str);
            if (n["label"]) node.label = n["label"].as<std::string>();

            // Tank params
            if (node.type == NodeType::tank) {
                TankParams tp;
                if (n["capacity_kg"])         tp.capacity_kg         = n["capacity_kg"].as<double>();
                if (n["unusable_kg"])         tp.unusable_kg         = n["unusable_kg"].as<double>();
                if (n["initial_quantity_kg"]) tp.initial_quantity_kg = n["initial_quantity_kg"].as<double>();
                else                         tp.initial_quantity_kg = tp.capacity_kg;
                if (n["arm_m"])              tp.arm_m               = n["arm_m"].as<double>();
                if (n["position_m"]) {
                    std::vector<double> pos;
                    for (const auto& v : n["position_m"])
                        pos.push_back(v.as<double>());
                    tp.position_m = pos;
                }
                node.tank = tp;
            }

            // Pump params
            if (node.type == NodeType::pump) {
                PumpParams pp;
                if (n["source"])       pp.source       = parsePumpSource(n["source"].as<std::string>());
                if (n["engine_index"]) pp.engine_index  = n["engine_index"].as<int>();
                if (n["min_rpm_pct"])  pp.min_rpm_pct   = n["min_rpm_pct"].as<double>();
                if (n["switch_id"])    pp.switch_id     = n["switch_id"].as<std::string>();
                node.pump = pp;
            }

            // Engine inlet
            if (node.type == NodeType::engine_inlet) {
                if (n["engine_index"]) node.engine_index = n["engine_index"].as<int>();
            }

            topology_.nodes.push_back(std::move(node));
        }
    }

    // ── Selectors ───────────────────────────────────────────────────
    if (root["selectors"]) {
        for (const auto& s : root["selectors"]) {
            SelectorDef sd;
            sd.id = s["id"].as<std::string>();
            if (s["switch_id"])        sd.switch_id        = s["switch_id"].as<std::string>();
            if (s["default_position"]) sd.default_position = s["default_position"].as<std::string>();
            if (s["positions"]) {
                for (const auto& p : s["positions"])
                    sd.positions.push_back(p.as<std::string>());
            }
            topology_.selectors.push_back(std::move(sd));
        }
    }

    // ── Connections ─────────────────────────────────────────────────
    if (root["connections"]) {
        for (const auto& c : root["connections"]) {
            Connection conn;
            conn.id = c["id"].as<std::string>();
            std::string conn_type_str = c["type"].as<std::string>();
            if (valid_conn_types.find(conn_type_str) == valid_conn_types.end()) {
                std::cerr << "[FuelGraphSolver] VALIDATION ERROR: connection '" << conn.id
                          << "' has unknown type '" << conn_type_str << "'" << std::endl;
                return false;
            }
            conn.type = parseConnectionType(conn_type_str);
            conn.from = c["from"].as<std::string>();
            conn.to   = c["to"].as<std::string>();

            if (c["switch_id"])    conn.switch_id    = c["switch_id"].as<std::string>();
            if (c["default_open"]) conn.default_open = c["default_open"].as<bool>();

            if (c["selector"]) {
                SelectorRef sr;
                sr.group = c["selector"]["group"].as<std::string>();
                if (c["selector"]["open_in"]) {
                    for (const auto& p : c["selector"]["open_in"])
                        sr.open_in.push_back(p.as<std::string>());
                }
                conn.selector = sr;
            }

            topology_.connections.push_back(std::move(conn));
        }
    }

    // ── CAS messages ────────────────────────────────────────────────
    if (root["cas_messages"]) {
        for (const auto& c : root["cas_messages"]) {
            CasMessageDef cm;
            cm.id   = c["id"].as<std::string>();
            cm.text = c["text"].as<std::string>();
            cm.level = c["level"].as<std::string>();
            if (c["condition"]) {
                auto cond = c["condition"];
                if (cond["type"])      cm.condition_type = cond["type"].as<std::string>();
                if (cond["target_id"]) cm.target_id      = cond["target_id"].as<std::string>();
                if (cond["threshold"]) cm.threshold      = cond["threshold"].as<double>();
            }
            topology_.cas_messages.push_back(std::move(cm));
        }
    }

    std::cout << "[FuelGraphSolver] Loaded: " << topology_.aircraft_type
              << " | " << topology_.nodes.size() << " nodes, "
              << topology_.connections.size() << " connections, "
              << topology_.selectors.size() << " selectors" << std::endl;

    if (!validateTopology()) return false;

    buildAdjacency();
    reset();
    return true;
}

// ─── Topology Validation ───────────────────────────────────────────

bool FuelGraphSolver::validateTopology() const {
    bool ok = true;
    auto err = [&](const std::string& msg) {
        std::cerr << "[FuelGraphSolver] VALIDATION ERROR: " << msg << std::endl;
        ok = false;
    };

    // Build node ID set + check duplicates
    std::unordered_set<std::string> node_ids;
    for (auto& n : topology_.nodes) {
        if (!node_ids.insert(n.id).second)
            err("duplicate node id '" + n.id + "'");
    }

    // Check connection duplicates + from/to references
    std::unordered_set<std::string> conn_ids;
    for (auto& c : topology_.connections) {
        if (!conn_ids.insert(c.id).second)
            err("duplicate connection id '" + c.id + "'");
        if (node_ids.find(c.from) == node_ids.end())
            err("connection '" + c.id + "' from '" + c.from + "' — node not found");
        if (node_ids.find(c.to) == node_ids.end())
            err("connection '" + c.id + "' to '" + c.to + "' — node not found");
    }

    // Validate selector references
    std::unordered_set<std::string> selector_ids;
    for (auto& sd : topology_.selectors) {
        if (!selector_ids.insert(sd.id).second)
            err("duplicate selector id '" + sd.id + "'");
        if (sd.positions.empty())
            err("selector '" + sd.id + "' has no positions");
        if (!sd.default_position.empty()) {
            bool found = false;
            for (auto& p : sd.positions) {
                if (p == sd.default_position) { found = true; break; }
            }
            if (!found)
                err("selector '" + sd.id + "' default_position '" + sd.default_position + "' not in positions list");
        }
    }

    // Validate selector references on connections
    for (auto& c : topology_.connections) {
        if (c.selector) {
            if (selector_ids.find(c.selector->group) == selector_ids.end())
                err("connection '" + c.id + "' references selector group '" + c.selector->group + "' — not found");
        }
    }

    // CAS target_id must reference a node or connection
    for (auto& cm : topology_.cas_messages) {
        if (!cm.target_id.empty() &&
            node_ids.find(cm.target_id) == node_ids.end() &&
            conn_ids.find(cm.target_id) == conn_ids.end())
            err("cas_message '" + cm.id + "' target_id '" + cm.target_id + "' — not found");
    }

    // Reachability check: BFS from tanks through all connections (ignoring state)
    std::unordered_map<std::string, std::vector<std::string>> adj;
    for (auto& c : topology_.connections) {
        adj[c.from].push_back(c.to);
        adj[c.to].push_back(c.from);
    }
    // Also treat pump nodes as passable in the check
    std::unordered_set<std::string> reachable;
    std::queue<std::string> q;
    for (auto& n : topology_.nodes) {
        if (n.type == NodeType::tank) {
            reachable.insert(n.id);
            q.push(n.id);
        }
    }
    while (!q.empty()) {
        auto cur = q.front(); q.pop();
        for (auto& neighbor : adj[cur]) {
            if (reachable.insert(neighbor).second)
                q.push(neighbor);
        }
    }
    for (auto& n : topology_.nodes) {
        if (n.type == NodeType::engine_inlet && reachable.find(n.id) == reachable.end())
            std::cout << "[FuelGraphSolver] WARNING: engine_inlet '" << n.id
                      << "' has no connection path from any tank" << std::endl;
    }

    return ok;
}

// ─── Build Adjacency Lists ─────────────────────────────────────────

void FuelGraphSolver::buildAdjacency() {
    node_index_.clear();
    conn_index_.clear();

    for (size_t i = 0; i < topology_.nodes.size(); ++i)
        node_index_[topology_.nodes[i].id] = i;
    for (size_t i = 0; i < topology_.connections.size(); ++i)
        conn_index_[topology_.connections[i].id] = i;

    size_t n = topology_.nodes.size();
    adjacency_.assign(n, {});

    for (size_t ci = 0; ci < topology_.connections.size(); ++ci) {
        auto& conn = topology_.connections[ci];
        auto fi = node_index_.find(conn.from);
        auto ti = node_index_.find(conn.to);
        if (fi != node_index_.end() && ti != node_index_.end()) {
            // Bidirectional for fuel flow (except check_valve — handled in propagation)
            adjacency_[fi->second].emplace_back(ci, ti->second);
            if (conn.type != ConnectionType::check_valve)
                adjacency_[ti->second].emplace_back(ci, fi->second);
        }
    }
}

// ─── Reset ──────────────────────────────────────────────────────────

void FuelGraphSolver::reset() {
    node_states_.clear();
    conn_states_.clear();
    failure_overrides_.clear();
    active_cas_.clear();
    selector_positions_.clear();
    pump_commands_.clear();
    valve_commands_.clear();

    for (auto& node : topology_.nodes) {
        NodeState ns;
        if (node.type == NodeType::tank && node.tank) {
            ns.quantity_kg = node.tank->initial_quantity_kg;
        }
        node_states_[node.id] = ns;
    }

    for (auto& conn : topology_.connections) {
        ConnectionState cs;
        cs.open = conn.default_open;
        conn_states_[conn.id] = cs;
    }

    // Set selectors to default positions
    for (auto& sd : topology_.selectors) {
        selector_positions_[sd.id] = sd.default_position;
    }

    // Size FDM input vectors
    int max_eng = 0;
    for (auto& node : topology_.nodes) {
        if (node.type == NodeType::engine_inlet)
            max_eng = std::max(max_eng, node.engine_index + 1);
        if (node.type == NodeType::pump && node.pump && node.pump->source == PumpSource::engine)
            max_eng = std::max(max_eng, node.pump->engine_index + 1);
    }
    fdm_inputs_.engine_rpm_pct.resize(max_eng, 0.0);
    fdm_inputs_.engine_fuel_demand_kgs.resize(max_eng, 0.0);
}

// ─── Main Solver Step ───────────────────────────────────────────────

void FuelGraphSolver::step(double dt) {
    updatePumpStates();
    updateConnectionStates();
    propagateFlow(dt);
    applyLeaks(dt);
    evaluateCas();
}

// ─── Update Pump States ─────────────────────────────────────────────

void FuelGraphSolver::updatePumpStates() {
    for (auto& node : topology_.nodes) {
        if (node.type != NodeType::pump || !node.pump) continue;
        auto& ns = node_states_[node.id];

        // Check failure override: force online
        if (hasOverride(node.id, "online")) {
            bool forced_online = (getOverride(node.id, "online") == "true");
            if (ns.jammed) {
                // Jammed — ignore everything
            } else {
                ns.running = forced_online;
            }
            continue;
        }

        if (ns.jammed) continue;  // Jammed — keep captured state

        auto& pp = *node.pump;
        if (pp.source == PumpSource::engine) {
            double rpm = 0.0;
            if (pp.engine_index >= 0 && pp.engine_index < static_cast<int>(fdm_inputs_.engine_rpm_pct.size()))
                rpm = fdm_inputs_.engine_rpm_pct[pp.engine_index];
            ns.running = (rpm >= pp.min_rpm_pct);
        } else {
            // Electrical pump: controlled by switch command
            auto it = pump_commands_.find(pp.switch_id);
            ns.running = (it != pump_commands_.end() && it->second);
        }
    }
}

// ─── Update Connection States ───────────────────────────────────────

void FuelGraphSolver::updateConnectionStates() {
    for (auto& conn : topology_.connections) {
        auto& cs = conn_states_[conn.id];

        if (cs.jammed) continue;  // Jammed — keep captured state

        switch (conn.type) {
            case ConnectionType::line:
                cs.open = true;
                break;
            case ConnectionType::valve: {
                auto it = valve_commands_.find(conn.switch_id);
                if (it != valve_commands_.end())
                    cs.open = it->second;
                else
                    cs.open = conn.default_open;
                break;
            }
            case ConnectionType::selector:
                cs.open = isSelectorConnectionOpen(conn);
                break;
            case ConnectionType::check_valve:
                cs.open = true;  // always open in forward direction, blocked in reverse by adjacency
                break;
        }
    }
}

bool FuelGraphSolver::isSelectorConnectionOpen(const Connection& conn) const {
    if (!conn.selector) return false;

    auto pos_it = selector_positions_.find(conn.selector->group);
    if (pos_it == selector_positions_.end()) return false;

    const std::string& current_pos = pos_it->second;
    for (auto& pos_name : conn.selector->open_in) {
        if (pos_name == current_pos) return true;
    }
    return false;
}

// ─── BFS Flow Propagation ───────────────────────────────────────────

void FuelGraphSolver::propagateFlow(double dt) {
    // Reset per-step state
    for (auto& node : topology_.nodes) {
        auto& ns = node_states_[node.id];
        ns.selected = false;
        if (node.type == NodeType::engine_inlet) {
            ns.fed = false;
            ns.fuel_flow_kgs = 0.0;
        }
    }

    // For each engine inlet, BFS backwards to find feeding tanks
    for (auto& inlet_node : topology_.nodes) {
        if (inlet_node.type != NodeType::engine_inlet) continue;
        auto& inlet_ns = node_states_[inlet_node.id];

        // Get fuel demand for this engine
        double demand_kgs = 0.0;
        if (inlet_node.engine_index >= 0 &&
            inlet_node.engine_index < static_cast<int>(fdm_inputs_.engine_fuel_demand_kgs.size()))
            demand_kgs = fdm_inputs_.engine_fuel_demand_kgs[inlet_node.engine_index];

        // BFS from engine_inlet backwards through open connections and running pumps
        std::unordered_set<size_t> visited;
        std::queue<size_t> q;
        std::vector<size_t> feeding_tank_indices;

        size_t inlet_idx = node_index_.at(inlet_node.id);
        visited.insert(inlet_idx);
        q.push(inlet_idx);

        while (!q.empty()) {
            size_t ni = q.front();
            q.pop();
            auto& node = topology_.nodes[ni];
            auto& ns = node_states_[node.id];

            // If this is a pump that's not running, stop propagation
            if (node.type == NodeType::pump && !ns.running) continue;

            // If this is a tank with usable fuel, it can feed
            if (node.type == NodeType::tank && node.tank) {
                double usable = ns.quantity_kg - node.tank->unusable_kg;
                if (usable > 0.0)
                    feeding_tank_indices.push_back(ni);
            }

            // Explore neighbors through open connections
            for (auto& [ci, neighbor_idx] : adjacency_[ni]) {
                if (visited.count(neighbor_idx)) continue;

                auto& conn = topology_.connections[ci];
                auto& cs = conn_states_[conn.id];

                if (!cs.open) continue;

                visited.insert(neighbor_idx);
                q.push(neighbor_idx);
            }
        }

        // Determine if engine is fed
        if (!feeding_tank_indices.empty()) {
            inlet_ns.fed = true;
            inlet_ns.fuel_flow_kgs = demand_kgs;

            // Drain equally from feeding tanks
            if (demand_kgs > 0.0 && dt > 0.0) {
                double drain_per_tank = (demand_kgs * dt) / static_cast<double>(feeding_tank_indices.size());
                for (size_t ti : feeding_tank_indices) {
                    auto& tank_node = topology_.nodes[ti];
                    auto& tank_ns = node_states_[tank_node.id];
                    tank_ns.quantity_kg = std::max(tank_node.tank->unusable_kg,
                                                   tank_ns.quantity_kg - drain_per_tank);
                    tank_ns.selected = true;
                }
            }

            // Mark all feeding tanks as selected even with zero demand
            for (size_t ti : feeding_tank_indices) {
                node_states_[topology_.nodes[ti].id].selected = true;
            }
        }
    }
}

// ─── Apply Leak Effects ─────────────────────────────────────────────

void FuelGraphSolver::applyLeaks(double dt) {
    if (dt <= 0.0) return;

    for (auto& conn : topology_.connections) {
        auto& cs = conn_states_[conn.id];
        if (cs.leak_rate_lph <= 0.0) continue;

        // Find upstream tanks by tracing back from conn.from
        auto from_it = node_index_.find(conn.from);
        if (from_it == node_index_.end()) continue;

        // BFS upstream to find tank(s) through open connections
        std::vector<size_t> upstream_tanks;
        std::unordered_set<size_t> visited;
        std::queue<size_t> q;
        visited.insert(from_it->second);
        q.push(from_it->second);
        while (!q.empty()) {
            size_t ni = q.front(); q.pop();
            auto& node = topology_.nodes[ni];
            if (node.type == NodeType::tank && node.tank) {
                upstream_tanks.push_back(ni);
                continue;  // Don't traverse past tanks
            }
            for (auto& [ci, neighbor_idx] : adjacency_[ni]) {
                if (visited.count(neighbor_idx)) continue;
                auto& leak_conn = topology_.connections[ci];
                auto& leak_cs = conn_states_[leak_conn.id];
                if (!leak_cs.open) continue;
                visited.insert(neighbor_idx);
                q.push(neighbor_idx);
            }
        }

        if (upstream_tanks.empty()) continue;

        double leak_kg_per_sec = (cs.leak_rate_lph * topology_.density_kg_per_liter) / 3600.0;
        double drain_per_tank = (leak_kg_per_sec * dt) / static_cast<double>(upstream_tanks.size());
        for (size_t ti : upstream_tanks) {
            auto& tank_node = topology_.nodes[ti];
            auto& tank_ns = node_states_[tank_node.id];
            tank_ns.quantity_kg = std::max(tank_node.tank->unusable_kg, tank_ns.quantity_kg - drain_per_tank);
        }
    }
}

// ─── CAS Evaluation ─────────────────────────────────────────────────

void FuelGraphSolver::evaluateCas() {
    active_cas_.clear();

    for (auto& cm : topology_.cas_messages) {
        bool triggered = false;
        int level = 0;
        if (cm.level == "advisory") level = 0;
        else if (cm.level == "caution") level = 1;
        else if (cm.level == "warning") level = 2;

        if (cm.condition_type == "low_fuel") {
            // Check if any/all tanks are below threshold
            if (cm.target_id.empty()) {
                // Global: total fuel norm below threshold
                double total = 0.0, capacity = 0.0;
                for (auto& n : topology_.nodes) {
                    if (n.type == NodeType::tank && n.tank) {
                        total += node_states_[n.id].quantity_kg;
                        capacity += n.tank->capacity_kg;
                    }
                }
                if (capacity > 0.0 && (total / capacity) < cm.threshold)
                    triggered = true;
            } else {
                // Specific tank
                auto it = node_states_.find(cm.target_id);
                auto ni = node_index_.find(cm.target_id);
                if (it != node_states_.end() && ni != node_index_.end()) {
                    auto& node = topology_.nodes[ni->second];
                    if (node.tank && node.tank->capacity_kg > 0.0) {
                        double norm = it->second.quantity_kg / node.tank->capacity_kg;
                        if (norm < cm.threshold) triggered = true;
                    }
                }
            }
        } else if (cm.condition_type == "fuel_press") {
            // Engine not fed → fuel pressure warning
            auto it = node_states_.find(cm.target_id);
            if (it != node_states_.end()) {
                auto ni = node_index_.find(cm.target_id);
                if (ni != node_index_.end() && topology_.nodes[ni->second].type == NodeType::engine_inlet) {
                    if (!it->second.fed) triggered = true;
                }
            }
        } else if (cm.condition_type == "pump_fail") {
            auto it = node_states_.find(cm.target_id);
            if (it != node_states_.end() && !it->second.running)
                triggered = true;
        }

        if (triggered)
            active_cas_.push_back({cm.text, level});
    }

    std::sort(active_cas_.begin(), active_cas_.end(),
              [](const CasMessage& a, const CasMessage& b) { return a.level > b.level; });
}

// ─── Snapshot ───────────────────────────────────────────────────────

FuelSnapshot FuelGraphSolver::getSnapshot() const {
    FuelSnapshot snap;

    double total_fuel = 0.0;
    double total_capacity = 0.0;
    double total_moment = 0.0;

    for (auto& node : topology_.nodes) {
        if (node.type == NodeType::tank && node.tank) {
            auto& ns = node_states_.at(node.id);
            TankSnapshot ts;
            ts.id = node.id;
            ts.quantity_kg = ns.quantity_kg;
            ts.capacity_kg = node.tank->capacity_kg;
            ts.unusable_kg = node.tank->unusable_kg;
            ts.quantity_norm = (node.tank->capacity_kg > 0.0)
                ? ns.quantity_kg / node.tank->capacity_kg : 0.0;
            ts.selected = ns.selected;
            snap.tanks.push_back(ts);

            total_fuel += ns.quantity_kg;
            total_capacity += node.tank->capacity_kg;
            total_moment += ns.quantity_kg * node.tank->arm_m;
        }

        if (node.type == NodeType::engine_inlet) {
            auto& ns = node_states_.at(node.id);
            EngineSnapshot es;
            es.engine_index = node.engine_index;
            es.fed = ns.fed;
            es.fuel_flow_kgs = ns.fuel_flow_kgs;
            snap.engines.push_back(es);
        }
    }

    snap.total_fuel_kg = total_fuel;
    snap.total_fuel_norm = (total_capacity > 0.0) ? total_fuel / total_capacity : 0.0;
    snap.low_fuel_warning = (snap.total_fuel_norm < topology_.low_fuel_threshold_norm);
    snap.cg_contribution_m = (total_fuel > 0.0) ? total_moment / total_fuel : 0.0;
    snap.cas_messages = active_cas_;

    return snap;
}

// ─── Commands ───────────────────────────────────────────────────────

void FuelGraphSolver::setFdmInputs(const FdmInputs& inputs) {
    fdm_inputs_ = inputs;
    // Ensure vectors are sized
    int max_eng = 0;
    for (auto& node : topology_.nodes) {
        if (node.type == NodeType::engine_inlet)
            max_eng = std::max(max_eng, node.engine_index + 1);
        if (node.type == NodeType::pump && node.pump && node.pump->source == PumpSource::engine)
            max_eng = std::max(max_eng, node.pump->engine_index + 1);
    }
    fdm_inputs_.engine_rpm_pct.resize(max_eng, 0.0);
    fdm_inputs_.engine_fuel_demand_kgs.resize(max_eng, 0.0);
}

void FuelGraphSolver::setSelector(const std::string& group_id, const std::string& position_name) {
    // Check if selector is jammed
    // Jam is on selector connections, but we check if the group has any jammed connections
    for (auto& conn : topology_.connections) {
        if (conn.selector && conn.selector->group == group_id) {
            auto& cs = conn_states_[conn.id];
            if (cs.jammed) return;  // Selector is jammed — ignore command
        }
    }

    // Validate position name
    for (auto& sd : topology_.selectors) {
        if (sd.id == group_id) {
            for (auto& p : sd.positions) {
                if (p == position_name) {
                    selector_positions_[group_id] = position_name;
                    return;
                }
            }
            std::cerr << "[FuelGraphSolver] Unknown position '" << position_name
                      << "' for selector '" << group_id << "'" << std::endl;
            return;
        }
    }
}

void FuelGraphSolver::commandPump(const std::string& switch_id, bool on) {
    pump_commands_[switch_id] = on;
}

void FuelGraphSolver::commandValve(const std::string& switch_id, bool open) {
    valve_commands_[switch_id] = open;
}

void FuelGraphSolver::setTankQuantity(const std::string& tank_id, double quantity_kg) {
    auto it = node_states_.find(tank_id);
    if (it != node_states_.end()) {
        auto idx_it = node_index_.find(tank_id);
        if (idx_it != node_index_.end() && topology_.nodes[idx_it->second].type == NodeType::tank) {
            it->second.quantity_kg = std::max(0.0, quantity_kg);
        }
    }
}

// ─── Failure Effects ────────────────────────────────────────────────

void FuelGraphSolver::applyFailureEffect(const std::string& target, const std::string& action,
                                         const std::string& property, const std::string& value) {
    failure_overrides_[{target, property}] = value;

    // Jam action on selector group: jam all connections in the group
    if (action == "jam") {
        for (auto& sd : topology_.selectors) {
            if (sd.id == target) {
                for (auto& conn : topology_.connections) {
                    if (conn.selector && conn.selector->group == target) {
                        auto& cs = conn_states_[conn.id];
                        cs.jammed = true;
                        cs.jammed_open = cs.open;
                    }
                }
                return;
            }
        }
    }

    // Jam on individual connection
    if (property == "jammed" && value == "true") {
        auto conn_it = conn_states_.find(target);
        if (conn_it != conn_states_.end()) {
            conn_it->second.jammed = true;
            conn_it->second.jammed_open = conn_it->second.open;
            return;
        }
        // Jam on pump node
        auto node_it = node_states_.find(target);
        if (node_it != node_states_.end()) {
            auto idx_it = node_index_.find(target);
            if (idx_it != node_index_.end() && topology_.nodes[idx_it->second].type == NodeType::pump) {
                node_it->second.jammed = true;
                node_it->second.jammed_running = node_it->second.running;
            }
        }
        return;
    }

    // Leak rate on connection
    if (property == "leak_rate_lph") {
        auto conn_it = conn_states_.find(target);
        if (conn_it != conn_states_.end()) {
            try {
                conn_it->second.leak_rate_lph = std::stod(value);
            } catch (...) {}
        }
    }

    // Force online on pump — handled in updatePumpStates via hasOverride
}

void FuelGraphSolver::clearFailureEffect(const std::string& target, const std::string& property) {
    failure_overrides_.erase({target, property});

    if (property == "jammed") {
        // Clear jam on connection
        auto conn_it = conn_states_.find(target);
        if (conn_it != conn_states_.end()) {
            conn_it->second.jammed = false;
        }

        // Clear jam on pump node
        auto node_it = node_states_.find(target);
        if (node_it != node_states_.end()) {
            node_it->second.jammed = false;
        }

        // Clear jam on selector group
        for (auto& sd : topology_.selectors) {
            if (sd.id == target) {
                for (auto& conn : topology_.connections) {
                    if (conn.selector && conn.selector->group == target) {
                        conn_states_[conn.id].jammed = false;
                    }
                }
            }
        }
    }

    if (property == "leak_rate_lph") {
        auto conn_it = conn_states_.find(target);
        if (conn_it != conn_states_.end())
            conn_it->second.leak_rate_lph = 0.0;
    }
}

void FuelGraphSolver::clearAllFailures() {
    failure_overrides_.clear();
    for (auto& [id, cs] : conn_states_) {
        cs.jammed = false;
        cs.leak_rate_lph = 0.0;
    }
    for (auto& [id, ns] : node_states_) {
        ns.jammed = false;
    }
}

// ─── Override Helpers ───────────────────────────────────────────────

std::string FuelGraphSolver::getOverride(const std::string& target, const std::string& property) const {
    auto it = failure_overrides_.find({target, property});
    return (it != failure_overrides_.end()) ? it->second : "";
}

bool FuelGraphSolver::hasOverride(const std::string& target, const std::string& property) const {
    return failure_overrides_.count({target, property}) > 0;
}

} // namespace fuel_graph
