#pragma once

#include "electrical/graph_types.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace elec_graph {

class GraphSolver {
public:
    GraphSolver();
    ~GraphSolver();

    /// Load topology from YAML v2 file. Returns true on success.
    bool loadTopologyYaml(const std::string& path);

    /// Reset all runtime state to initial conditions.
    void reset();

    /// Run one solver step. dt is in seconds.
    void step(double dt);

    /// Set FDM inputs (call before step()).
    void setFdmInputs(const FdmInputs& inputs);

    /// Command a connection (switch, CB, contactor). 0=open, 1=close, 2=toggle.
    void commandConnection(const std::string& id, int cmd);

    /// Set a selector to a specific position.
    void setSelector(const std::string& group_id, int position_index);

    /// Set a potentiometer value (0.0–1.0).
    void setPotentiometer(const std::string& id, double value);

    /// Apply a failure effect override on a node or connection property.
    void applyFailureEffect(const std::string& target, const std::string& action,
                            const std::string& property, const std::string& value);

    /// Clear a specific failure effect.
    void clearFailureEffect(const std::string& target, const std::string& property);

    /// Clear all failure effects and reset GCU / jam states.
    void clearAllFailures();

    /// Read state
    const std::unordered_map<std::string, NodeState>& getNodeStates() const { return node_states_; }
    const std::unordered_map<std::string, ConnectionState>& getConnectionStates() const { return conn_states_; }
    const std::vector<CasMessage>& getCasMessages() const { return active_cas_; }
    const Topology& getTopology() const { return topology_; }

private:
    bool validateTopology() const;
    void buildAdjacency();
    void updateSources(double dt);
    void resetAllNodes();
    void propagate();
    void updateRelayCoils();
    void updateLoads(double dt);
    void updateBatterySoc(double dt);
    void evaluateCas();

    double interpolateSocVoltage(const BatteryParams& bp, double soc) const;
    double detectChargingVoltage(const Node& battery_node) const;
    std::string getOverride(const std::string& target, const std::string& property) const;
    bool hasOverride(const std::string& target, const std::string& property) const;

    Topology topology_;
    FdmInputs fdm_inputs_;

    // Index maps: id → vector index
    std::unordered_map<std::string, size_t> node_index_;
    std::unordered_map<std::string, size_t> conn_index_;

    // Forward adjacency: node_idx → [(conn_idx, neighbor_node_idx)]
    std::vector<std::vector<std::pair<size_t, size_t>>> adjacency_;
    // Reverse adjacency: node_idx → [(conn_idx, upstream_node_idx)]
    std::vector<std::vector<std::pair<size_t, size_t>>> reverse_adj_;

    // Runtime state keyed by element id
    std::unordered_map<std::string, NodeState> node_states_;
    std::unordered_map<std::string, ConnectionState> conn_states_;

    // Failure overrides: (target_id, property) → value string
    std::map<std::pair<std::string, std::string>, std::string> failure_overrides_;

    std::vector<CasMessage> active_cas_;
    double sim_time_ = 0.0;
};

} // namespace elec_graph
