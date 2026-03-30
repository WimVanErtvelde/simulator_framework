#include <sim_interfaces/i_electrical_model.hpp>
#include <electrical/graph_solver.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <string>

namespace aircraft_c172
{

class ElectricalModel : public sim_interfaces::IElectricalModel
{
public:
  void configure(const std::string & yaml_path) override
  {
    solver_.loadTopologyYaml(yaml_path);
  }

  void update(double dt_sec) override
  {
    solver_.step(dt_sec);
  }

  void apply_failure(const std::string & failure_id, bool active) override
  {
    // Parse "target/fault_type" format (electrical_node always sends "target/fail")
    std::string target = failure_id;
    auto sep = failure_id.find('/');
    if (sep != std::string::npos)
      target = failure_id.substr(0, sep);

    if (active) {
      // Determine element type and apply appropriate effect
      auto& ns = solver_.getNodeStates();
      auto& cs = solver_.getConnectionStates();
      if (ns.count(target)) {
        // Source node → force offline
        solver_.applyFailureEffect(target, "set", "online", "false");
      } else if (cs.count(target)) {
        // Connection (CB, switch, etc.) → force tripped
        solver_.applyFailureEffect(target, "set", "tripped", "true");
      }
    } else {
      // Clear all known effects for this target
      solver_.clearFailureEffect(target, "online");
      solver_.clearFailureEffect(target, "tripped");
      solver_.clearFailureEffect(target, "jammed");
    }
  }

  void command_switch(const std::string & id, int cmd) override
  {
    solver_.commandConnection(id, cmd);
  }

  void set_engine_n2(const std::vector<double> & n2_pct) override
  {
    fdm_inputs_.engine_n2_pct = n2_pct;
    solver_.setFdmInputs(fdm_inputs_);
  }

  void set_ground_state(bool on_ground, bool external_power_connected) override
  {
    fdm_inputs_.on_ground = on_ground;
    fdm_inputs_.external_power_connected = external_power_connected;
    solver_.setFdmInputs(fdm_inputs_);
  }

  void reset() override
  {
    solver_.reset();
    fdm_inputs_ = elec_graph::FdmInputs{};
  }

  sim_interfaces::ElectricalSnapshot get_snapshot() const override
  {
    sim_interfaces::ElectricalSnapshot snap;
    const auto & topo = solver_.getTopology();
    const auto & ns = solver_.getNodeStates();
    const auto & cs = solver_.getConnectionStates();

    float total_amps = 0.0f;

    for (const auto & node : topo.nodes) {
      auto it = ns.find(node.id);
      if (it == ns.end()) continue;
      const auto & s = it->second;

      switch (node.type) {
        case elec_graph::NodeType::bus:
          snap.buses.push_back({node.id, static_cast<float>(s.voltage), s.powered});
          if (node.id == "primary_bus")   snap.master_bus_voltage = static_cast<float>(s.voltage);
          if (node.id == "avionics_bus")  snap.avionics_bus_powered = s.powered;
          if (node.id == "essential_bus") snap.essential_bus_powered = s.powered;
          break;

        case elec_graph::NodeType::source:
          snap.sources.push_back({node.id, s.online, static_cast<float>(s.voltage),
                                  static_cast<float>(s.current), static_cast<float>(s.battery_soc)});
          if (node.source && node.source->subtype == "battery" && s.battery_soc >= 0)
            snap.battery_soc_pct = static_cast<float>(s.battery_soc);
          break;

        case elec_graph::NodeType::load:
          snap.loads.push_back({node.id, s.powered, static_cast<float>(s.current)});
          if (s.powered) total_amps += static_cast<float>(s.current);
          break;

        default:
          break;
      }
    }

    // Switches and CBs from connections
    for (const auto & conn : topo.connections) {
      auto it = cs.find(conn.id);
      if (it == cs.end()) continue;
      const auto & s = it->second;

      switch (conn.type) {
        case elec_graph::ConnectionType::switch_:
        case elec_graph::ConnectionType::contactor:
        case elec_graph::ConnectionType::relay:
          snap.switches.push_back({conn.id, conn.label, s.closed});
          break;

        case elec_graph::ConnectionType::circuit_breaker:
          snap.cbs.push_back({conn.id, s.closed && !s.pulled, s.tripped});
          break;

        default:
          break;
      }
    }

    snap.total_load_amps = total_amps;
    return snap;
  }

private:
  elec_graph::GraphSolver solver_;
  elec_graph::FdmInputs fdm_inputs_;
};

}  // namespace aircraft_c172

PLUGINLIB_EXPORT_CLASS(aircraft_c172::ElectricalModel, sim_interfaces::IElectricalModel)
