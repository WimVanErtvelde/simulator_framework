#pragma once

#include "fuel/fuel_graph_types.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fuel_graph {

class FuelGraphSolver {
public:
    FuelGraphSolver();
    ~FuelGraphSolver();

    /// Load topology from YAML v2 file. Returns true on success.
    bool loadTopologyYaml(const std::string& path);

    /// Reset all runtime state to initial conditions.
    void reset();

    /// Run one solver step. dt is in seconds.
    void step(double dt);

    /// Set FDM inputs (call before step()).
    void setFdmInputs(const FdmInputs& inputs);

    /// Set a selector to a named position.
    void setSelector(const std::string& group_id, const std::string& position_name);

    /// Command a pump on/off (for electrical pumps via switch_id).
    void commandPump(const std::string& switch_id, bool on);

    /// Command a valve open/close (via switch_id).
    void commandValve(const std::string& switch_id, bool open);

    /// Set a tank's fuel quantity directly (for apply_initial_conditions).
    void setTankQuantity(const std::string& tank_id, double quantity_kg);

    /// Apply a failure effect override on a node or connection property.
    void applyFailureEffect(const std::string& target, const std::string& action,
                            const std::string& property, const std::string& value);

    /// Clear a specific failure effect.
    void clearFailureEffect(const std::string& target, const std::string& property);

    /// Clear all failure effects.
    void clearAllFailures();

    /// Build a snapshot of the current fuel state (for node to map to FuelState.msg).
    FuelSnapshot getSnapshot() const;

    /// Read state
    const std::unordered_map<std::string, NodeState>& getNodeStates() const { return node_states_; }
    const std::unordered_map<std::string, ConnectionState>& getConnectionStates() const { return conn_states_; }
    const std::vector<CasMessage>& getCasMessages() const { return active_cas_; }
    const Topology& getTopology() const { return topology_; }

private:
    bool validateTopology() const;
    void buildAdjacency();
    void updatePumpStates();
    void updateConnectionStates();
    void propagateFlow(double dt);
    void applyLeaks(double dt);
    void evaluateCas();

    std::string getOverride(const std::string& target, const std::string& property) const;
    bool hasOverride(const std::string& target, const std::string& property) const;

    bool isSelectorConnectionOpen(const Connection& conn) const;

    Topology topology_;
    FdmInputs fdm_inputs_;

    // Selector runtime: group_id → current position name
    std::unordered_map<std::string, std::string> selector_positions_;

    // Pump command state: switch_id → on/off
    std::unordered_map<std::string, bool> pump_commands_;

    // Valve command state: switch_id → open/closed
    std::unordered_map<std::string, bool> valve_commands_;

    // Index maps: id → vector index
    std::unordered_map<std::string, size_t> node_index_;
    std::unordered_map<std::string, size_t> conn_index_;

    // Bidirectional adjacency: node_idx → [(conn_idx, neighbor_node_idx)]
    std::vector<std::vector<std::pair<size_t, size_t>>> adjacency_;

    // Runtime state keyed by element id
    std::unordered_map<std::string, NodeState> node_states_;
    std::unordered_map<std::string, ConnectionState> conn_states_;

    // Failure overrides: (target_id, property) → value string
    std::map<std::pair<std::string, std::string>, std::string> failure_overrides_;

    std::vector<CasMessage> active_cas_;
};

} // namespace fuel_graph
