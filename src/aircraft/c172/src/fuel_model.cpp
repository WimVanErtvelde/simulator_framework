#include <sim_interfaces/i_fuel_model.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace aircraft_c172
{

struct TankDef {
  std::string id;
  float capacity_kg = 0.0f;
  float unusable_kg = 0.0f;
  float arm_m = 0.0f;
};

struct FeedDef {
  int engine_index = 0;
  std::string selector_id;  // empty if no selector
  // position value -> "both", tank_id, or "null" (off)
  std::map<int, std::string> positions;
};

struct BoostPumpDef {
  std::string id;  // switch ID (sw_ prefix)
  std::string tank;
};

class FuelModel : public sim_interfaces::IFuelModel
{
public:
  void configure(const std::string & yaml_path) override
  {
    YAML::Node root = YAML::LoadFile(yaml_path);
    auto fuel = root["fuel"];

    // Load tanks
    for (const auto & t : fuel["tanks"]) {
      TankDef td;
      td.id = t["id"].as<std::string>();
      td.capacity_kg = t["capacity_kg"].as<float>();
      td.unusable_kg = t["unusable_kg"].as<float>(0.0f);
      td.arm_m = t["arm_m"].as<float>(0.0f);
      tanks_.push_back(td);
    }

    // Load feed rules
    if (fuel["feed"]) {
      for (const auto & f : fuel["feed"]) {
        FeedDef fd;
        fd.engine_index = f["engine_index"].as<int>();
        if (f["selector_id"] && !f["selector_id"].IsNull()) {
          fd.selector_id = f["selector_id"].as<std::string>();
        }
        if (f["positions"]) {
          for (const auto & p : f["positions"]) {
            int pos = p.first.as<int>();
            if (p.second.IsNull()) {
              fd.positions[pos] = "null";
            } else {
              fd.positions[pos] = p.second.as<std::string>();
            }
          }
        }
        feeds_.push_back(fd);
      }
    }

    // Load boost pumps
    if (fuel["boost_pumps"]) {
      for (const auto & b : fuel["boost_pumps"]) {
        BoostPumpDef bp;
        bp.id = b["id"].as<std::string>();
        bp.tank = b["tank"].as<std::string>();
        boost_pumps_.push_back(bp);
      }
    }

    low_fuel_threshold_pct_ = fuel["low_fuel_threshold_pct"].as<float>(0.10f);

    // Initialize tanks to 100%
    tank_qty_kg_.resize(tanks_.size(), 0.0f);
    for (size_t i = 0; i < tanks_.size(); ++i) {
      tank_qty_kg_[i] = tanks_[i].capacity_kg;
    }

    // Default selector to position 0 (both)
    selector_position_ = 0;

    std::cout << "[FuelModel C172] Configured: " << tanks_.size() << " tanks" << std::endl;
  }

  void update(
      double dt_sec,
      const std::vector<float> & engine_fuel_flow_kgs,
      const sim_msgs::msg::PanelControls & panel,
      const std::vector<std::string> & active_failures) override
  {
    // Update selector position from panel
    for (const auto & feed : feeds_) {
      if (feed.selector_id.empty()) continue;
      for (size_t i = 0; i < panel.selector_ids.size(); ++i) {
        if (panel.selector_ids[i] == feed.selector_id) {
          selector_position_ = panel.selector_values[i];
          break;
        }
      }
    }

    // Update boost pump state from panel switches
    boost_pump_on_.clear();
    boost_pump_on_.resize(boost_pumps_.size(), false);
    for (size_t i = 0; i < boost_pumps_.size(); ++i) {
      for (size_t j = 0; j < panel.switch_ids.size(); ++j) {
        if (panel.switch_ids[j] == boost_pumps_[i].id) {
          boost_pump_on_[i] = panel.switch_states[j];
          break;
        }
      }
    }

    // Check for fuel system failures
    bool fuel_leak = false;
    for (const auto & f : active_failures) {
      if (f.find("fuel_leak") != std::string::npos) fuel_leak = true;
    }

    // Integrate fuel consumption per engine
    engine_fuel_flow_kgs_.assign(4, 0.0f);
    for (const auto & feed : feeds_) {
      if (feed.engine_index < 0 || feed.engine_index >= 4) continue;

      float flow = 0.0f;
      if (feed.engine_index < static_cast<int>(engine_fuel_flow_kgs.size())) {
        flow = engine_fuel_flow_kgs[feed.engine_index];
      }
      engine_fuel_flow_kgs_[feed.engine_index] = flow;

      if (flow <= 0.0f) continue;

      float consumption = flow * static_cast<float>(dt_sec);

      // Determine which tanks to drain based on selector
      std::vector<int> drain_tanks;
      if (!feed.selector_id.empty() && !feed.positions.empty()) {
        auto it = feed.positions.find(selector_position_);
        if (it != feed.positions.end()) {
          if (it->second == "null") {
            continue;  // fuel off
          } else if (it->second == "both") {
            // Drain all tanks equally
            for (size_t i = 0; i < tanks_.size(); ++i) {
              drain_tanks.push_back(static_cast<int>(i));
            }
          } else {
            // Find specific tank
            for (size_t i = 0; i < tanks_.size(); ++i) {
              if (tanks_[i].id == it->second) {
                drain_tanks.push_back(static_cast<int>(i));
                break;
              }
            }
          }
        }
      } else {
        // No selector — drain all tanks equally
        for (size_t i = 0; i < tanks_.size(); ++i) {
          drain_tanks.push_back(static_cast<int>(i));
        }
      }

      if (drain_tanks.empty()) continue;

      float per_tank = consumption / static_cast<float>(drain_tanks.size());
      for (int idx : drain_tanks) {
        tank_qty_kg_[idx] = std::max(0.0f, tank_qty_kg_[idx] - per_tank);
      }
    }

    // Fuel leak effect — lose 0.5 kg/s from first tank
    if (fuel_leak && !tanks_.empty()) {
      tank_qty_kg_[0] = std::max(0.0f,
        tank_qty_kg_[0] - 0.5f * static_cast<float>(dt_sec));
    }
  }

  void apply_initial_conditions(float fuel_total_pct) override
  {
    float pct = std::clamp(fuel_total_pct, 0.0f, 1.0f);
    for (size_t i = 0; i < tanks_.size(); ++i) {
      tank_qty_kg_[i] = tanks_[i].capacity_kg * pct;
    }
  }

  sim_msgs::msg::FuelState get_state() const override
  {
    sim_msgs::msg::FuelState state;

    float total_kg = 0.0f;
    float total_capacity = 0.0f;
    float cg_moment = 0.0f;

    for (size_t i = 0; i < tanks_.size() && i < 4; ++i) {
      state.tank_quantity_kg[i] = tank_qty_kg_[i];
      state.tank_quantity_pct[i] =
        (tanks_[i].capacity_kg > 0.0f) ? tank_qty_kg_[i] / tanks_[i].capacity_kg : 0.0f;
      float usable = std::max(0.0f, tank_qty_kg_[i] - tanks_[i].unusable_kg);
      state.tank_usable_kg[i] = usable;
      total_kg += tank_qty_kg_[i];
      total_capacity += tanks_[i].capacity_kg;
      cg_moment += tank_qty_kg_[i] * tanks_[i].arm_m;

      // Determine if tank is selected
      state.tank_selected[i] = false;
    }

    // Determine selected tanks from current selector position
    for (const auto & feed : feeds_) {
      if (!feed.selector_id.empty() && !feed.positions.empty()) {
        auto it = feed.positions.find(selector_position_);
        if (it != feed.positions.end()) {
          if (it->second == "both") {
            for (size_t i = 0; i < tanks_.size() && i < 4; ++i) {
              state.tank_selected[i] = true;
            }
          } else if (it->second != "null") {
            for (size_t i = 0; i < tanks_.size() && i < 4; ++i) {
              if (tanks_[i].id == it->second) {
                state.tank_selected[i] = true;
              }
            }
          }
        }
      } else {
        // No selector — all tanks feed
        for (size_t i = 0; i < tanks_.size() && i < 4; ++i) {
          state.tank_selected[i] = true;
        }
      }
    }

    state.total_fuel_kg = total_kg;
    state.total_fuel_pct = (total_capacity > 0.0f) ? total_kg / total_capacity : 0.0f;
    state.cg_contribution_m = (total_kg > 0.0f) ? cg_moment / total_kg : 0.0f;

    state.low_fuel_warning = (total_capacity > 0.0f) &&
      (total_kg / total_capacity < low_fuel_threshold_pct_);

    // Copy engine fuel flow
    for (size_t i = 0; i < 4; ++i) {
      state.engine_fuel_flow_kgs[i] = (i < engine_fuel_flow_kgs_.size()) ?
        engine_fuel_flow_kgs_[i] : 0.0f;
    }

    // Fuel pressure: non-zero if boost pump is on and tank has usable fuel
    for (size_t i = 0; i < 4; ++i) {
      state.fuel_pressure_pa[i] = 0.0f;
      state.boost_pump_on[i] = false;
    }
    for (size_t i = 0; i < boost_pumps_.size() && i < 4; ++i) {
      bool on = (i < boost_pump_on_.size()) ? boost_pump_on_[i] : false;
      state.boost_pump_on[i] = on;
      if (on) {
        // Find the tank this pump serves
        for (size_t t = 0; t < tanks_.size(); ++t) {
          if (tanks_[t].id == boost_pumps_[i].tank) {
            float usable = std::max(0.0f, tank_qty_kg_[t] - tanks_[t].unusable_kg);
            state.fuel_pressure_pa[i] = (usable > 0.0f) ? 35000.0f : 0.0f;
            break;
          }
        }
      }
    }

    return state;
  }

private:
  std::vector<TankDef> tanks_;
  std::vector<FeedDef> feeds_;
  std::vector<BoostPumpDef> boost_pumps_;
  std::vector<float> tank_qty_kg_;
  std::vector<float> engine_fuel_flow_kgs_;
  std::vector<bool> boost_pump_on_;
  float low_fuel_threshold_pct_ = 0.10f;
  int selector_position_ = 0;
};

}  // namespace aircraft_c172

PLUGINLIB_EXPORT_CLASS(aircraft_c172::FuelModel, sim_interfaces::IFuelModel)
