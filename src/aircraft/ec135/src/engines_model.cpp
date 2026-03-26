#include <sim_interfaces/i_engines_model.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>
#include <algorithm>

namespace aircraft_ec135
{

static constexpr float PSI_TO_KPA = 6.89476f;

class EnginesModel : public sim_interfaces::IEnginesModel
{
public:
  void configure(const std::string & yaml_path) override
  {
    auto cfg = YAML::LoadFile(yaml_path);
    engine_count_ = static_cast<uint8_t>(cfg["engine_count"].as<int>(2));

    sw_cfg_ = {};
    auto engines = cfg["engines"];
    if (engines && engines.IsSequence()) {
      for (size_t i = 0; i < engines.size() && i < 4; ++i) {
        auto e = engines[i];
        tgt_max_degc_[i] = e["tgt_max_degc"].as<float>(810.0f);
        torque_max_pct_[i] = e["torque_max_pct"].as<float>(100.0f);
        sw_cfg_.starter_ids.push_back(e["starter_id"].as<std::string>(""));
        sw_cfg_.ignition_ids.push_back(e["ignition_id"].as<std::string>(""));
        sw_cfg_.fuel_cutoff_ids.push_back(e["fuel_cutoff_id"].as<std::string>(""));
        sw_cfg_.prop_lever_ids.push_back(e["prop_lever_id"].as<std::string>(""));
        sw_cfg_.condition_lever_ids.push_back(e["condition_lever_id"].as<std::string>(""));
        sw_cfg_.power_lever_ids.push_back(e["power_lever_id"].as<std::string>(""));
      }
    }

    reset();
  }

  void update(
      double /*dt_sec*/,
      const sim_interfaces::EngineInputs & inputs,
      const sim_msgs::msg::FlightModelState & fdm,
      const std::vector<std::string> & active_failures) override
  {
    for (int e = 0; e < engine_count_ && e < 4; ++e) {
      // ── Read FDM engine data (passthrough) ──────────────────────
      if (e < static_cast<int>(fdm.engine_count)) {
        state_.n1_pct[e] = fdm.n1_pct[e];
        state_.n2_pct[e] = fdm.n2_pct[e];
        state_.tot_degc[e] = fdm.engine_egt_degc[e];   // ITT/TGT from adapter
        state_.torque_pct[e] = fdm.torque_pct[e];
        state_.oil_press_kpa[e] = fdm.engine_oil_pressure_psi[e] * PSI_TO_KPA;
        state_.oil_temp_degc[e] = fdm.engine_oil_temp_degc[e];
        state_.fuel_flow_kgph[e] = fdm.fuel_flow_kgs[e] * 3600.0f;

        bool fdm_running = (fdm.engine_status_flags[e] & 0x01) != 0;
        engine_running_[e] = fdm_running;
      }

      // ── Cockpit logic: condition lever OFF kills engine ──────────
      if (inputs.condition_lever_norm[e] < 0.01f) {
        engine_running_[e] = false;
      }

      // ── Engine state machine ────────────────────────────────────
      if (!engine_running_[e]) {
        if (inputs.starter[e] && inputs.condition_lever_norm[e] > 0.01f &&
            inputs.fuel_available[e] && inputs.bus_voltage > 18.0f) {
          state_.state[e] = sim_interfaces::EngineRunState::CRANKING;
        } else {
          state_.state[e] = sim_interfaces::EngineRunState::OFF;
        }
      } else {
        state_.state[e] = sim_interfaces::EngineRunState::RUNNING;
      }

      state_.starter_engaged[e] = inputs.starter[e];

      // Piston-specific fields not applicable for turboshaft
      state_.engine_rpm[e] = 0.0f;
      state_.egt_degc[e] = 0.0f;
      state_.cht_degc[e] = 0.0f;
      state_.manifold_press_inhg[e] = 0.0f;

      // ── Failure overrides ───────────────────────────────────────
      std::string fail_prefix = "engine_" + std::to_string(e);
      for (const auto & f : active_failures) {
        if (f == fail_prefix + "_oil_pressure") {
          state_.oil_press_kpa[e] = 0.0f;
        } else if (f == fail_prefix + "_failure") {
          engine_running_[e] = false;
          state_.state[e] = sim_interfaces::EngineRunState::FAILED;
        }
      }

      // ── Warnings ────────────────────────────────────────────────
      state_.low_oil_pressure_warning[e] =
          engine_running_[e] && state_.oil_press_kpa[e] < (25.0f * PSI_TO_KPA);
      state_.high_egt_warning[e] = state_.tot_degc[e] > tgt_max_degc_[e];
      state_.high_cht_warning[e] = false;  // N/A for turboshaft
    }

    // Drivetrain — rotor RPM from FDM (driven by both engines through gearbox)
    if (fdm.rotor_rpm.size() > 0) {
      state_.main_rotor_rpm = fdm.rotor_rpm[0];
    }
    if (fdm.rotor_rpm.size() > 1) {
      state_.tail_rotor_rpm = fdm.rotor_rpm[1];
    }

    state_.engine_count = engine_count_;
  }

  void set_running(uint8_t engine_index, bool running) override
  {
    if (engine_index < 4) {
      engine_running_[engine_index] = running;
      state_.state[engine_index] = running
        ? sim_interfaces::EngineRunState::RUNNING
        : sim_interfaces::EngineRunState::OFF;
    }
  }

  uint8_t get_engine_count() const override { return engine_count_; }

  void reset() override
  {
    state_ = {};
    state_.engine_count = engine_count_;
    for (int e = 0; e < 4; ++e) {
      engine_running_[e] = false;
    }
  }

  sim_interfaces::EngineStateData get_state() const override { return state_; }

  sim_interfaces::EngineSwitchConfig get_switch_config() const override { return sw_cfg_; }

private:
  sim_interfaces::EngineStateData state_;
  uint8_t engine_count_ = 2;

  // Per-engine YAML config
  float tgt_max_degc_[4] = {810.0f, 810.0f, 810.0f, 810.0f};
  float torque_max_pct_[4] = {100.0f, 100.0f, 100.0f, 100.0f};

  sim_interfaces::EngineSwitchConfig sw_cfg_;

  // Runtime state
  bool engine_running_[4] = {};
};

}  // namespace aircraft_ec135

PLUGINLIB_EXPORT_CLASS(aircraft_ec135::EnginesModel, sim_interfaces::IEnginesModel)
