#include "electrical/graph_solver.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <queue>
#include <unordered_set>

namespace elec_graph {

// ─── Helpers ────────────────────────────────────────────────────────

static NodeType parseNodeType(const std::string& s) {
    if (s == "source")   return NodeType::source;
    if (s == "bus")      return NodeType::bus;
    if (s == "junction") return NodeType::junction;
    if (s == "load")     return NodeType::load;
    return NodeType::junction;
}

static const std::unordered_set<std::string> valid_conn_types = {
    "wire", "switch", "contactor", "relay", "circuit_breaker", "potentiometer", "selector"
};

static ConnectionType parseConnectionType(const std::string& s) {
    if (s == "wire")            return ConnectionType::wire;
    if (s == "switch")          return ConnectionType::switch_;
    if (s == "contactor")       return ConnectionType::contactor;
    if (s == "relay")           return ConnectionType::relay;
    if (s == "circuit_breaker") return ConnectionType::circuit_breaker;
    if (s == "potentiometer")   return ConnectionType::potentiometer;
    if (s == "selector")        return ConnectionType::selector;
    return ConnectionType::wire;
}

static const std::unordered_set<std::string> valid_source_subtypes = {
    "battery", "dc_generator", "ac_generator", "starter_generator", "apu_generator", "external_power"
};

static bool typeDefaultClosed(ConnectionType type) {
    return type == ConnectionType::wire || type == ConnectionType::circuit_breaker;
}

static bool isPassable(ConnectionType type, const ConnectionState& cs) {
    if (type == ConnectionType::wire) return true;
    if (cs.jammed) return cs.jammed_value;

    switch (type) {
        case ConnectionType::circuit_breaker:
            return cs.closed && !cs.tripped && !cs.pulled;
        case ConnectionType::switch_:
        case ConnectionType::contactor:
        case ConnectionType::relay:
            return cs.closed && !cs.tripped;
        case ConnectionType::potentiometer:
            return cs.pot_value > 0.0;
        case ConnectionType::selector:
            return cs.closed;
        default:
            return cs.closed;
    }
}

// ─── Constructor / Destructor ───────────────────────────────────────

GraphSolver::GraphSolver() {}
GraphSolver::~GraphSolver() {}

// ─── YAML v2 Loader ─────────────────────────────────────────────────

bool GraphSolver::loadTopologyYaml(const std::string& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        std::cerr << "[GraphSolver] YAML parse error: " << e.what() << std::endl;
        return false;
    }

    topology_ = Topology{};

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
    }

    // ── Nodes ───────────────────────────────────────────────────────
    if (root["nodes"]) {
        for (const auto& n : root["nodes"]) {
            Node node;
            node.id   = n["id"].as<std::string>();
            node.type = parseNodeType(n["type"].as<std::string>());
            if (n["label"]) node.label = n["label"].as<std::string>();

            // Source params
            if (node.type == NodeType::source) {
                SourceParams sp;
                if (n["subtype"])         sp.subtype         = n["subtype"].as<std::string>();
                if (n["nominal_voltage"]) sp.nominal_voltage = n["nominal_voltage"].as<double>();
                if (n["engine_index"])    sp.engine_index    = n["engine_index"].as<int>();
                if (n["min_rpm_pct"])     sp.min_rpm_pct     = n["min_rpm_pct"].as<double>();

                if (n["gcu"]) {
                    GcuParams gcu;
                    auto g = n["gcu"];
                    if (g["overvolt_threshold"])  gcu.overvolt_threshold  = g["overvolt_threshold"].as<double>();
                    if (g["undervolt_threshold"]) gcu.undervolt_threshold = g["undervolt_threshold"].as<double>();
                    if (g["trip_delay_ms"])       gcu.trip_delay_ms       = g["trip_delay_ms"].as<double>();
                    if (g["reset_mode"])          gcu.reset_mode          = g["reset_mode"].as<std::string>();
                    sp.gcu = gcu;
                }

                if (n["battery"]) {
                    BatteryParams bp;
                    auto b = n["battery"];
                    if (b["capacity_ah"])             bp.capacity_ah             = b["capacity_ah"].as<double>();
                    if (b["internal_resistance_ohm"]) bp.internal_resistance_ohm = b["internal_resistance_ohm"].as<double>();
                    if (b["charge_rate_max"])         bp.charge_rate_max         = b["charge_rate_max"].as<double>();
                    if (b["initial_soc"])             bp.initial_soc             = b["initial_soc"].as<double>();
                    if (b["soc_voltage_curve"]) {
                        for (const auto& pt : b["soc_voltage_curve"]) {
                            bp.soc_voltage_curve.emplace_back(pt[0].as<double>(), pt[1].as<double>());
                        }
                    }
                    sp.battery = bp;
                }

                node.source = sp;
            }

            // Load params
            if (node.type == NodeType::load) {
                LoadParams lp;
                if (n["nominal_current"]) lp.nominal_current = n["nominal_current"].as<double>();
                node.load = lp;
            }

            topology_.nodes.push_back(std::move(node));
        }
    }

    // ── Connections ─────────────────────────────────────────────────
    if (root["connections"]) {
        for (const auto& c : root["connections"]) {
            Connection conn;
            conn.id   = c["id"].as<std::string>();
            std::string conn_type_str = c["type"].as<std::string>();
            if (valid_conn_types.find(conn_type_str) == valid_conn_types.end()) {
                std::cerr << "[GraphSolver] VALIDATION ERROR: connection '" << conn.id
                          << "' has unknown type '" << conn_type_str << "'" << std::endl;
                return false;
            }
            conn.type = parseConnectionType(conn_type_str);
            conn.from = c["from"].as<std::string>();
            conn.to   = c["to"].as<std::string>();

            if (c["label"])              conn.label              = c["label"].as<std::string>();
            if (c["default_closed"])     conn.default_closed     = c["default_closed"].as<bool>();
            else                         conn.default_closed     = typeDefaultClosed(conn.type);
            if (c["pilot_controllable"]) conn.pilot_controllable = c["pilot_controllable"].as<bool>();
            if (c["rating"])             conn.rating             = c["rating"].as<double>();
            if (c["coil_bus"])           conn.coil_bus           = c["coil_bus"].as<std::string>();

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

    std::cout << "[GraphSolver] Loaded: " << topology_.aircraft_type
              << " | " << topology_.nodes.size() << " nodes, "
              << topology_.connections.size() << " connections" << std::endl;

    if (!validateTopology()) return false;

    buildAdjacency();
    reset();
    return true;
}

// ─── Topology Validation ───────────────────────────────────────────

bool GraphSolver::validateTopology() const {
    bool ok = true;
    auto err = [&](const std::string& msg) {
        std::cerr << "[GraphSolver] VALIDATION ERROR: " << msg << std::endl;
        ok = false;
    };

    // Build node ID set + check duplicates
    std::unordered_map<std::string, NodeType> node_ids;
    for (auto& n : topology_.nodes) {
        if (!node_ids.emplace(n.id, n.type).second)
            err("duplicate node id '" + n.id + "'");
        // Source subtype validation
        if (n.type == NodeType::source) {
            std::string subtype = n.source ? n.source->subtype : "";
            if (subtype.empty() || valid_source_subtypes.find(subtype) == valid_source_subtypes.end())
                err("source node '" + n.id + "' has unknown subtype '" + subtype + "'");
        }
    }

    // Check connection duplicates + from/to/coil_bus references
    std::unordered_set<std::string> conn_ids;
    for (auto& c : topology_.connections) {
        if (!conn_ids.insert(c.id).second)
            err("duplicate connection id '" + c.id + "'");
        if (node_ids.find(c.from) == node_ids.end())
            err("connection '" + c.id + "' from '" + c.from + "' — node not found");
        if (node_ids.find(c.to) == node_ids.end())
            err("connection '" + c.id + "' to '" + c.to + "' — node not found");
        if (!c.coil_bus.empty()) {
            auto it = node_ids.find(c.coil_bus);
            if (it == node_ids.end())
                err("connection '" + c.id + "' coil_bus '" + c.coil_bus + "' — node not found");
            else if (it->second != NodeType::bus)
                err("connection '" + c.id + "' coil_bus '" + c.coil_bus + "' — not a bus node");
        }
    }

    // CAS target_id must reference a node or connection
    for (auto& cm : topology_.cas_messages) {
        if (!cm.target_id.empty() &&
            node_ids.find(cm.target_id) == node_ids.end() &&
            conn_ids.find(cm.target_id) == conn_ids.end())
            err("cas_message '" + cm.id + "' target_id '" + cm.target_id + "' — not found");
    }

    // Load reachability: BFS from all sources through topology (ignoring switch state)
    // Build adjacency on node IDs for this check
    std::unordered_map<std::string, std::vector<std::string>> adj;
    for (auto& c : topology_.connections) {
        adj[c.from].push_back(c.to);
        adj[c.to].push_back(c.from);  // bidirectional — power can flow either way
    }
    std::unordered_set<std::string> reachable;
    std::queue<std::string> q;
    for (auto& n : topology_.nodes) {
        if (n.type == NodeType::source) {
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
        if (n.type == NodeType::load && reachable.find(n.id) == reachable.end())
            std::cout << "[GraphSolver] WARNING: load '" << n.id
                      << "' has no connection path from any source" << std::endl;
    }

    return ok;
}

// ─── Build Adjacency Lists ─────────────────────────────────────────

void GraphSolver::buildAdjacency() {
    node_index_.clear();
    conn_index_.clear();

    for (size_t i = 0; i < topology_.nodes.size(); ++i)
        node_index_[topology_.nodes[i].id] = i;
    for (size_t i = 0; i < topology_.connections.size(); ++i)
        conn_index_[topology_.connections[i].id] = i;

    size_t n = topology_.nodes.size();
    adjacency_.assign(n, {});
    reverse_adj_.assign(n, {});

    for (size_t ci = 0; ci < topology_.connections.size(); ++ci) {
        auto& conn = topology_.connections[ci];
        auto fi = node_index_.find(conn.from);
        auto ti = node_index_.find(conn.to);
        if (fi != node_index_.end() && ti != node_index_.end()) {
            adjacency_[fi->second].emplace_back(ci, ti->second);
            reverse_adj_[ti->second].emplace_back(ci, fi->second);
        }
    }

}

// ─── Reset ──────────────────────────────────────────────────────────

void GraphSolver::reset() {
    node_states_.clear();
    conn_states_.clear();
    failure_overrides_.clear();
    active_cas_.clear();
    sim_time_ = 0.0;

    for (auto& node : topology_.nodes) {
        NodeState ns;
        if (node.type == NodeType::source && node.source && node.source->battery) {
            ns.battery_soc = node.source->battery->initial_soc;
        }
        node_states_[node.id] = ns;
    }

    for (auto& conn : topology_.connections) {
        ConnectionState cs;
        cs.closed = conn.default_closed;
        conn_states_[conn.id] = cs;
    }

    // Size engine input vector
    int max_eng = 0;
    for (auto& node : topology_.nodes) {
        if (node.type == NodeType::source && node.source && node.source->engine_index >= 0)
            max_eng = std::max(max_eng, node.source->engine_index + 1);
    }
    fdm_inputs_.engine_n2_pct.resize(max_eng, 0.0);
}

// ─── Main Solver Step ───────────────────────────────────────────────

void GraphSolver::step(double dt) {
    updateSources(dt);

    // Detect charging state from previous frame's propagation, but AFTER
    // updateSources has applied failure overrides (so offline sources are
    // already offline and won't contribute stale voltage to the BFS).
    // Then re-set battery voltage if charging detected, before propagate.
    if (!adjacency_.empty() && !node_states_.empty()) {
        for (auto& node : topology_.nodes) {
            if (node.type != NodeType::source || !node.source || !node.source->battery) continue;
            double cv = detectChargingVoltage(node);
            if (cv > 0.0) {
                auto& ns = node_states_[node.id];
                auto& bp = *node.source->battery;
                double charge_rate = std::min(bp.charge_rate_max,
                                              (100.0 - ns.battery_soc) * 0.5);
                ns.voltage = cv - (bp.internal_resistance_ohm * charge_rate);
            }
        }
    }

    for (int pass = 0; pass < topology_.propagation_passes; ++pass) {
        resetAllNodes();
        propagate();
        updateRelayCoils();
    }

    updateLoads(dt);
    updateBatterySoc(dt);
    evaluateCas();

    sim_time_ += dt;
}

// ─── Source Models ──────────────────────────────────────────────────

void GraphSolver::updateSources(double dt) {
    for (auto& node : topology_.nodes) {
        if (node.type != NodeType::source || !node.source) continue;
        auto& ns = node_states_[node.id];
        auto& sp = *node.source;

        // Failure override: force offline
        if (hasOverride(node.id, "online") && getOverride(node.id, "online") == "false") {
            ns.online = false;
            ns.voltage = 0.0;
            ns.current = 0.0;
            continue;
        }

        if (sp.subtype == "dc_generator" || sp.subtype == "ac_generator" ||
            sp.subtype == "starter_generator") {

            double n2 = 0.0;
            if (sp.engine_index >= 0 && sp.engine_index < static_cast<int>(fdm_inputs_.engine_n2_pct.size()))
                n2 = fdm_inputs_.engine_n2_pct[sp.engine_index];

            bool rpm_ok = (n2 >= sp.min_rpm_pct);

            // GCU protection
            if (sp.gcu && !ns.gcu_tripped) {
                double v = ns.voltage;
                bool limit_exceeded = false;
                if (v > 0 && v > sp.gcu->overvolt_threshold) limit_exceeded = true;
                if (v > 0 && v < sp.gcu->undervolt_threshold && rpm_ok) limit_exceeded = true;
                if (limit_exceeded) {
                    ns.gcu_trip_timer += dt * 1000.0;
                    if (ns.gcu_trip_timer >= sp.gcu->trip_delay_ms) ns.gcu_tripped = true;
                } else {
                    ns.gcu_trip_timer = 0.0;
                }
            }

            ns.online = rpm_ok && !ns.gcu_tripped;
            if (ns.online) {
                double rpm_factor = std::min(1.0, (n2 - sp.min_rpm_pct) / (100.0 - sp.min_rpm_pct));
                ns.voltage = sp.nominal_voltage * (0.95 + 0.05 * rpm_factor);
            } else {
                ns.voltage = 0.0;
            }

        } else if (sp.subtype == "apu_generator") {
            ns.online  = fdm_inputs_.apu_running;
            ns.voltage = ns.online ? sp.nominal_voltage : 0.0;

        } else if (sp.subtype == "external_power") {
            ns.online  = fdm_inputs_.external_power_connected;
            ns.voltage = ns.online ? sp.nominal_voltage : 0.0;

        } else if (sp.subtype == "battery") {
            ns.online = true;
            if (sp.battery) {
                double ocv = interpolateSocVoltage(*sp.battery, ns.battery_soc);
                double load_current = ns.current; // from previous frame
                ns.voltage = std::max(0.0, ocv - load_current * sp.battery->internal_resistance_ohm);
            } else {
                ns.voltage = sp.nominal_voltage * (0.8 + 0.2 * (ns.battery_soc / 100.0));
            }
        }
    }
}

double GraphSolver::interpolateSocVoltage(const BatteryParams& bp, double soc) const {
    if (bp.soc_voltage_curve.empty())
        return 24.0 * (0.8 + 0.2 * (soc / 100.0));

    soc = std::clamp(soc, 0.0, 100.0);
    auto& curve = bp.soc_voltage_curve;
    if (soc <= curve.front().first) return curve.front().second;
    if (soc >= curve.back().first)  return curve.back().second;

    for (size_t i = 0; i + 1 < curve.size(); ++i) {
        if (soc >= curve[i].first && soc <= curve[i + 1].first) {
            double t = (soc - curve[i].first) / (curve[i + 1].first - curve[i].first);
            return curve[i].second + t * (curve[i + 1].second - curve[i].second);
        }
    }
    return curve.back().second;
}

// ─── Reset Non-Source Nodes ─────────────────────────────────────────

void GraphSolver::resetAllNodes() {
    for (auto& node : topology_.nodes) {
        if (node.type == NodeType::source) continue;
        auto& ns = node_states_[node.id];
        ns.powered = false;
        ns.voltage = 0.0;
        ns.current = 0.0;
        ns.power_source.clear();
    }
}

// ─── BFS Power Propagation ─────────────────────────────────────────

void GraphSolver::propagate() {
    std::queue<size_t> q;

    // Seed queue with online sources
    for (size_t i = 0; i < topology_.nodes.size(); ++i) {
        auto& node = topology_.nodes[i];
        if (node.type == NodeType::source) {
            auto& ns = node_states_[node.id];
            if (ns.online && ns.voltage > 0.0) {
                ns.powered = true;
                ns.power_source = node.id;
                q.push(i);
            }
        }
    }

    // BFS: propagate through passable connections
    while (!q.empty()) {
        size_t ni = q.front();
        q.pop();
        auto& ns = node_states_[topology_.nodes[ni].id];

        for (auto& [ci, neighbor_idx] : adjacency_[ni]) {
            auto& conn = topology_.connections[ci];
            auto& cs   = conn_states_[conn.id];

            if (!isPassable(conn.type, cs)) continue;

            auto& neighbor_ns = node_states_[topology_.nodes[neighbor_idx].id];
            if (ns.voltage > neighbor_ns.voltage) {
                neighbor_ns.powered = true;
                neighbor_ns.voltage = ns.voltage;
                neighbor_ns.power_source = ns.power_source;
                q.push(neighbor_idx);
            }
        }
    }
}

// ─── Relay Coil Logic ───────────────────────────────────────────────

void GraphSolver::updateRelayCoils() {
    for (auto& conn : topology_.connections) {
        if (conn.type != ConnectionType::relay || conn.coil_bus.empty()) continue;
        auto& cs = conn_states_[conn.id];
        if (cs.jammed) continue;

        auto coil_it = node_states_.find(conn.coil_bus);
        if (coil_it != node_states_.end())
            cs.closed = coil_it->second.powered;
    }
}

// ─── Load Updates ───────────────────────────────────────────────────

void GraphSolver::updateLoads(double /*dt*/) {
    for (auto& node : topology_.nodes) {
        if (node.type != NodeType::load || !node.load) continue;
        auto& ns = node_states_[node.id];

        if (!ns.powered) {
            ns.current = 0.0;
            continue;
        }

        ns.current = node.load->nominal_current;
    }
}

// ─── Battery SOC ────────────────────────────────────────────────────

void GraphSolver::updateBatterySoc(double dt) {
    for (auto& node : topology_.nodes) {
        if (node.type != NodeType::source || !node.source || !node.source->battery) continue;
        auto& ns = node_states_[node.id];
        if (ns.battery_soc < 0) continue;
        auto& bp = *node.source->battery;

        // Drain: sum current drawn from all loads powered by this battery
        double drain_current = 0.0;
        for (auto& lnode : topology_.nodes) {
            if (lnode.type != NodeType::load) continue;
            auto& lns = node_states_[lnode.id];
            if (lns.powered && lns.power_source == node.id)
                drain_current += lns.current;
        }
        ns.current = drain_current;

        // Charge: BFS from battery — find max voltage from any reachable non-battery source
        double charge_current = 0.0;
        {
            double charge_voltage = 0.0;
            std::unordered_set<size_t> visited;
            std::queue<size_t> cq;
            auto batt_it = node_index_.find(node.id);
            if (batt_it != node_index_.end()) {
                cq.push(batt_it->second);
                visited.insert(batt_it->second);
                while (!cq.empty()) {
                    size_t ni = cq.front(); cq.pop();
                    for (auto& [ci, neighbor_idx] : adjacency_[ni]) {
                        if (visited.count(neighbor_idx)) continue;
                        auto& conn = topology_.connections[ci];
                        auto& cs = conn_states_[conn.id];
                        if (!isPassable(conn.type, cs)) continue;
                        visited.insert(neighbor_idx);
                        auto& nns = node_states_[topology_.nodes[neighbor_idx].id];
                        if (nns.powered && nns.power_source != node.id) {
                            charge_voltage = std::max(charge_voltage, nns.voltage);
                        }
                        cq.push(neighbor_idx);
                    }
                }
            }
            if (charge_voltage > 0.0) {
                double ocv = interpolateSocVoltage(bp, ns.battery_soc);
                charge_current = (charge_voltage - ocv) / bp.internal_resistance_ohm;
                charge_current = std::clamp(charge_current, 0.0, bp.charge_rate_max);
            }
        }

        // Net current: positive = net charging, negative = net discharging
        double net_current = charge_current - drain_current;
        if (net_current > 0.0) {
            double soc_gain = (net_current * dt) / (bp.capacity_ah * 3600.0) * 100.0;
            ns.battery_soc = std::min(100.0, ns.battery_soc + soc_gain);
        } else if (net_current < 0.0) {
            double soc_drain = ((-net_current) * dt) / (bp.capacity_ah * 3600.0) * 100.0;
            ns.battery_soc = std::max(0.0, ns.battery_soc - soc_drain);
        }
    }
}

// ─── CAS Evaluation ─────────────────────────────────────────────────

void GraphSolver::evaluateCas() {
    active_cas_.clear();

    for (auto& cm : topology_.cas_messages) {
        bool triggered = false;
        int level = 0;
        if (cm.level == "advisory") level = 0;
        else if (cm.level == "caution") level = 1;
        else if (cm.level == "warning") level = 2;

        if (cm.condition_type == "source_fail") {
            auto it = node_states_.find(cm.target_id);
            if (it != node_states_.end()) {
                if (hasOverride(cm.target_id, "online") && !it->second.online)
                    triggered = true;
                if (it->second.gcu_tripped)
                    triggered = true;
            }
        } else if (cm.condition_type == "low_voltage") {
            auto it = node_states_.find(cm.target_id);
            if (it != node_states_.end() && it->second.powered &&
                it->second.voltage < cm.threshold)
                triggered = true;
        } else if (cm.condition_type == "low_soc") {
            auto it = node_states_.find(cm.target_id);
            if (it != node_states_.end() && it->second.battery_soc >= 0 &&
                it->second.battery_soc < cm.threshold)
                triggered = true;
        } else if (cm.condition_type == "bus_dead") {
            auto it = node_states_.find(cm.target_id);
            if (it != node_states_.end() && !it->second.powered)
                triggered = true;
        } else if (cm.condition_type == "cb_trip") {
            auto it = conn_states_.find(cm.target_id);
            if (it != conn_states_.end() && it->second.tripped)
                triggered = true;
        }

        if (triggered)
            active_cas_.push_back({cm.text, level});
    }

    std::sort(active_cas_.begin(), active_cas_.end(),
              [](const CasMessage& a, const CasMessage& b) { return a.level > b.level; });
}

// ─── Commands ───────────────────────────────────────────────────────

void GraphSolver::setFdmInputs(const FdmInputs& inputs) {
    fdm_inputs_ = inputs;
    int max_eng = 0;
    for (auto& node : topology_.nodes) {
        if (node.type == NodeType::source && node.source && node.source->engine_index >= 0)
            max_eng = std::max(max_eng, node.source->engine_index + 1);
    }
    fdm_inputs_.engine_n2_pct.resize(max_eng, 0.0);
}

void GraphSolver::commandConnection(const std::string& id, int cmd) {
    auto it = conn_states_.find(id);
    if (it == conn_states_.end()) return;
    auto& cs = it->second;

    if (cs.jammed) return;

    auto ci = conn_index_.find(id);
    if (ci == conn_index_.end()) return;
    auto& conn = topology_.connections[ci->second];

    // Wire: not commandable
    if (conn.type == ConnectionType::wire) return;

    // Relay with coil: driven by coil logic only
    if (conn.type == ConnectionType::relay && !conn.coil_bus.empty()) return;

    if (conn.type == ConnectionType::circuit_breaker) {
        switch (cmd) {
            case 0: cs.pulled = true;  cs.closed = false; break;
            case 1: cs.closed = true;  cs.pulled = false; cs.tripped = false; break;
            case 2:
                if (cs.tripped) { cs.tripped = false; cs.closed = true; cs.pulled = false; }
                else { cs.pulled = !cs.pulled; cs.closed = !cs.pulled; }
                break;
        }
    } else {
        switch (cmd) {
            case 0: cs.closed = false; break;
            case 1: cs.closed = true;  break;
            case 2: cs.closed = !cs.closed; break;
        }
    }
}

void GraphSolver::setSelector(const std::string& group_id, int position_index) {
    auto it = conn_states_.find(group_id);
    if (it != conn_states_.end())
        it->second.selector_position = position_index;
}

void GraphSolver::setPotentiometer(const std::string& id, double value) {
    auto it = conn_states_.find(id);
    if (it != conn_states_.end())
        it->second.pot_value = std::clamp(value, 0.0, 1.0);
}

// ─── Failure Effects ────────────────────────────────────────────────

void GraphSolver::applyFailureEffect(const std::string& target, const std::string& /*action*/,
                                     const std::string& property, const std::string& value) {
    failure_overrides_[{target, property}] = value;

    // Immediate state update for jam
    if (property == "jammed") {
        auto it = conn_states_.find(target);
        if (it != conn_states_.end()) {
            it->second.jammed = (value == "true");
            if (it->second.jammed)
                it->second.jammed_value = it->second.closed;
        }
    }

    // Immediate state update for CB trip
    if (property == "tripped") {
        auto it = conn_states_.find(target);
        if (it != conn_states_.end()) {
            it->second.tripped = (value == "true");
            if (it->second.tripped)
                it->second.closed = false;
        }
    }
}

void GraphSolver::clearFailureEffect(const std::string& target, const std::string& property) {
    failure_overrides_.erase({target, property});

    if (property == "jammed") {
        auto it = conn_states_.find(target);
        if (it != conn_states_.end())
            it->second.jammed = false;
    }

    if (property == "tripped") {
        auto it = conn_states_.find(target);
        if (it != conn_states_.end()) {
            it->second.tripped = false;
            it->second.closed = true;
        }
    }
}

void GraphSolver::clearAllFailures() {
    failure_overrides_.clear();
    for (auto& [id, cs] : conn_states_)
        cs.jammed = false;
    for (auto& [id, ns] : node_states_) {
        ns.gcu_tripped = false;
        ns.gcu_trip_timer = 0.0;
    }
}

// ─── Override Helpers ───────────────────────────────────────────────

std::string GraphSolver::getOverride(const std::string& target, const std::string& property) const {
    auto it = failure_overrides_.find({target, property});
    return (it != failure_overrides_.end()) ? it->second : "";
}

bool GraphSolver::hasOverride(const std::string& target, const std::string& property) const {
    return failure_overrides_.count({target, property}) > 0;
}

double GraphSolver::detectChargingVoltage(const Node& battery_node) const {
    if (adjacency_.empty() || node_states_.empty()) return 0.0;

    auto batt_it = node_index_.find(battery_node.id);
    if (batt_it == node_index_.end()) return 0.0;
    if (batt_it->second >= adjacency_.size()) return 0.0;

    double max_voltage = 0.0;
    std::unordered_set<size_t> visited;
    std::queue<size_t> q;
    q.push(batt_it->second);
    visited.insert(batt_it->second);

    while (!q.empty()) {
        size_t ni = q.front(); q.pop();
        if (ni >= adjacency_.size()) continue;
        for (auto& [ci, neighbor_idx] : adjacency_[ni]) {
            if (visited.count(neighbor_idx)) continue;
            if (ci >= topology_.connections.size()) continue;
            if (neighbor_idx >= topology_.nodes.size()) continue;
            auto& conn = topology_.connections[ci];
            auto cs_it = conn_states_.find(conn.id);
            if (cs_it == conn_states_.end()) continue;
            if (!isPassable(conn.type, cs_it->second)) continue;
            visited.insert(neighbor_idx);
            auto nns_it = node_states_.find(topology_.nodes[neighbor_idx].id);
            if (nns_it == node_states_.end()) continue;
            auto& nns = nns_it->second;
            if (nns.powered && nns.power_source != battery_node.id) {
                auto src_it = node_states_.find(nns.power_source);
                if (src_it != node_states_.end() && src_it->second.online && src_it->second.voltage > 0.0) {
                    max_voltage = std::max(max_voltage, nns.voltage);
                }
            }
            q.push(neighbor_idx);
        }
    }
    return max_voltage;
}

} // namespace elec_graph
