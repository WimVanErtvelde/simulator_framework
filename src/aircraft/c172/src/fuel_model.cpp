#include <sim_interfaces/i_fuel_model.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <fuel/fuel_graph_solver.hpp>

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

namespace aircraft_c172
{

class FuelModel : public sim_interfaces::IFuelModel
{
public:
  void configure(const std::string & yaml_path) override
  {
    solver_ = std::make_unique<fuel_graph::FuelGraphSolver>();
    if (!solver_->loadTopologyYaml(yaml_path)) {
      std::cerr << "[FuelModel C172] Failed to load fuel topology: " << yaml_path << std::endl;
      solver_.reset();
      return;
    }

    auto & topo = solver_->getTopology();
    density_ = topo.density_kg_per_liter;

    // Cache tank IDs and capacities for get_state mapping
    tank_ids_.clear();
    for (auto & node : topo.nodes) {
      if (node.type == fuel_graph::NodeType::tank && node.tank) {
        tank_ids_.push_back(node.id);
      }
    }

    // Cache electric pump switch IDs
    elec_pump_switch_ids_.clear();
    for (auto & node : topo.nodes) {
      if (node.type == fuel_graph::NodeType::pump && node.pump &&
          node.pump->source == fuel_graph::PumpSource::electrical) {
        elec_pump_switch_ids_.push_back(node.pump->switch_id);
      }
    }

    // Cache selector info
    selector_defs_.clear();
    for (auto & sd : topo.selectors) {
      selector_defs_.push_back(sd);
    }

    std::cout << "[FuelModel C172] Configured: graph solver with "
              << topo.nodes.size() << " nodes, "
              << topo.connections.size() << " connections" << std::endl;
  }

  void update(
      double dt_sec,
      const std::vector<float> & engine_fuel_flow_kgs,
      const sim_msgs::msg::PanelControls & panel,
      const std::vector<std::string> & active_failures) override
  {
    if (!solver_) return;

    // Build FDM inputs
    fuel_graph::FdmInputs fdm;

    // Engine RPM: option (b) — if flow > 0, engine is running above pump threshold
    fdm.engine_rpm_pct.resize(engine_fuel_flow_kgs.size(), 0.0);
    for (size_t i = 0; i < engine_fuel_flow_kgs.size(); ++i) {
      fdm.engine_rpm_pct[i] = (engine_fuel_flow_kgs[i] > 0.0f) ? 100.0 : 0.0;
    }

    // Engine fuel demand
    fdm.engine_fuel_demand_kgs.resize(engine_fuel_flow_kgs.size(), 0.0);
    for (size_t i = 0; i < engine_fuel_flow_kgs.size(); ++i) {
      fdm.engine_fuel_demand_kgs[i] = static_cast<double>(engine_fuel_flow_kgs[i]);
    }

    solver_->setFdmInputs(fdm);

    // Map panel switches to electric pump commands
    for (auto & sw_id : elec_pump_switch_ids_) {
      for (size_t j = 0; j < panel.switch_ids.size(); ++j) {
        if (panel.switch_ids[j] == sw_id) {
          solver_->commandPump(sw_id, panel.switch_states[j]);
          break;
        }
      }
    }

    // Map panel selectors to solver selector positions
    for (auto & sd : selector_defs_) {
      for (size_t j = 0; j < panel.selector_ids.size(); ++j) {
        if (panel.selector_ids[j] == sd.switch_id) {
          int pos_idx = panel.selector_values[j];
          if (pos_idx >= 0 && pos_idx < static_cast<int>(sd.positions.size())) {
            solver_->setSelector(sd.id, sd.positions[pos_idx]);
          }
          break;
        }
      }
    }

    // Process failures
    processFailures(active_failures);

    solver_->step(dt_sec);
  }

  void apply_initial_conditions(float fuel_total_norm) override
  {
    if (!solver_) return;
    float pct = std::clamp(fuel_total_norm, 0.0f, 1.0f);
    auto & topo = solver_->getTopology();
    for (auto & node : topo.nodes) {
      if (node.type == fuel_graph::NodeType::tank && node.tank) {
        solver_->setTankQuantity(node.id, node.tank->capacity_kg * pct);
      }
    }
  }

  sim_msgs::msg::FuelState get_state() const override
  {
    sim_msgs::msg::FuelState state;
    if (!solver_) return state;

    auto snap = solver_->getSnapshot();

    // Per-tank quantities
    for (size_t i = 0; i < snap.tanks.size() && i < 8; ++i) {
      state.tank_quantity_kg[i] = static_cast<float>(snap.tanks[i].quantity_kg);
      state.tank_quantity_norm[i] = static_cast<float>(snap.tanks[i].quantity_norm);
      float usable = std::max(0.0f,
        static_cast<float>(snap.tanks[i].quantity_kg - snap.tanks[i].unusable_kg));
      state.tank_usable_kg[i] = usable;
      state.tank_selected[i] = snap.tanks[i].selected;
    }

    // Totals
    state.total_fuel_kg = static_cast<float>(snap.total_fuel_kg);
    state.total_fuel_norm = static_cast<float>(snap.total_fuel_norm);
    state.cg_contribution_m = static_cast<float>(snap.cg_contribution_m);
    state.low_fuel_warning = snap.low_fuel_warning;

    // Per-engine fuel flow and pressure
    for (size_t i = 0; i < snap.engines.size() && i < 4; ++i) {
      int idx = snap.engines[i].engine_index;
      if (idx >= 0 && idx < 4) {
        state.engine_fuel_flow_kgs[idx] = static_cast<float>(snap.engines[i].fuel_flow_kgs);
        state.fuel_pressure_pa[idx] = snap.engines[i].fed ? 35000.0f : 0.0f;
      }
    }

    // Boost pump state from electric pump node
    auto & node_states = solver_->getNodeStates();
    for (size_t i = 0; i < elec_pump_switch_ids_.size() && i < 4; ++i) {
      // Find the pump node by switch_id
      for (auto & node : solver_->getTopology().nodes) {
        if (node.type == fuel_graph::NodeType::pump && node.pump &&
            node.pump->source == fuel_graph::PumpSource::electrical &&
            node.pump->switch_id == elec_pump_switch_ids_[i]) {
          auto it = node_states.find(node.id);
          if (it != node_states.end()) {
            state.boost_pump_on[i] = it->second.running;
          }
          break;
        }
      }
    }

    return state;
  }

  void reset() override
  {
    if (solver_) {
      solver_->reset();
    }
    prev_failures_.clear();
  }

private:
  void processFailures(const std::vector<std::string> & active_failures)
  {
    if (!solver_) return;

    // Find newly added failures
    for (auto & f : active_failures) {
      if (std::find(prev_failures_.begin(), prev_failures_.end(), f) == prev_failures_.end()) {
        applyFailure(f);
      }
    }

    // Find removed failures
    for (auto & f : prev_failures_) {
      if (std::find(active_failures.begin(), active_failures.end(), f) == active_failures.end()) {
        clearFailure(f);
      }
    }

    prev_failures_ = active_failures;
  }

  void applyFailure(const std::string & failure_id)
  {
    // Failure IDs follow pattern: target.property or target.action.property
    // For fuel: "pump_mechanical.online.false", "sel_fuel.jam", "line_X.leak_rate_lph.10"
    auto dot1 = failure_id.find('.');
    if (dot1 == std::string::npos) return;

    std::string target = failure_id.substr(0, dot1);
    std::string rest = failure_id.substr(dot1 + 1);

    auto dot2 = rest.find('.');
    if (dot2 != std::string::npos) {
      std::string prop = rest.substr(0, dot2);
      std::string val = rest.substr(dot2 + 1);
      solver_->applyFailureEffect(target, "force", prop, val);
    } else {
      // Single property after target: e.g. "sel_fuel.jam"
      if (rest == "jam") {
        solver_->applyFailureEffect(target, "jam", "jammed", "true");
      } else {
        solver_->applyFailureEffect(target, "force", rest, "true");
      }
    }
  }

  void clearFailure(const std::string & failure_id)
  {
    auto dot1 = failure_id.find('.');
    if (dot1 == std::string::npos) return;

    std::string target = failure_id.substr(0, dot1);
    std::string rest = failure_id.substr(dot1 + 1);

    auto dot2 = rest.find('.');
    if (dot2 != std::string::npos) {
      std::string prop = rest.substr(0, dot2);
      solver_->clearFailureEffect(target, prop);
    } else {
      if (rest == "jam") {
        solver_->clearFailureEffect(target, "jammed");
      } else {
        solver_->clearFailureEffect(target, rest);
      }
    }
  }

  std::unique_ptr<fuel_graph::FuelGraphSolver> solver_;
  double density_ = 0.72;
  std::vector<std::string> tank_ids_;
  std::vector<std::string> elec_pump_switch_ids_;
  std::vector<fuel_graph::SelectorDef> selector_defs_;
  std::vector<std::string> prev_failures_;
};

}  // namespace aircraft_c172

PLUGINLIB_EXPORT_CLASS(aircraft_c172::FuelModel, sim_interfaces::IFuelModel)
