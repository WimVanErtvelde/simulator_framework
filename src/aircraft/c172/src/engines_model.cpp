#include <sim_interfaces/i_engines_model.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace aircraft_c172
{

static constexpr float PSI_TO_KPA = 6.89476f;
static constexpr float GPH_TO_KGPH = 2.72f;  // avgas ~0.72 kg/L, 1 US gal = 3.785 L

class EnginesModel : public sim_interfaces::IEnginesModel
{
public:
  void configure(const std::string & yaml_path) override
  {
    auto cfg = YAML::LoadFile(yaml_path);
    engine_count_ = static_cast<uint8_t>(cfg["engine_count"].as<int>(1));

    sw_cfg_ = {};
    auto engines = cfg["engines"];
    if (engines && engines.IsSequence()) {
      for (size_t i = 0; i < engines.size() && i < 4; ++i) {
        auto e = engines[i];
        if (i == 0) {
          rpm_max_ = e["rpm_max"].as<float>(2700.0f);
          rpm_idle_ = e["rpm_idle"].as<float>(700.0f);
          egt_max_degc_ = e["egt_max_degc"].as<float>(900.0f);
          cht_max_degc_ = e["cht_max_degc"].as<float>(240.0f);
          oil_pressure_min_psi_ = e["oil_pressure_min_psi"].as<float>(25.0f);
          oil_temp_max_degc_ = e["oil_temp_max_degc"].as<float>(118.0f);
        }
        sw_cfg_.starter_ids.push_back(e["starter_id"].as<std::string>(""));
        sw_cfg_.ignition_ids.push_back(e["magneto_switch_id"].as<std::string>(
            e["ignition_id"].as<std::string>("")));
        sw_cfg_.fuel_cutoff_ids.push_back(e["fuel_cutoff_id"].as<std::string>(""));
        sw_cfg_.prop_lever_ids.push_back(e["prop_lever_id"].as<std::string>(""));
        sw_cfg_.condition_lever_ids.push_back(e["condition_lever_id"].as<std::string>(""));
        sw_cfg_.power_lever_ids.push_back(e["power_lever_id"].as<std::string>(""));
      }
    }

    reset();
  }

  void update(
      double dt_sec,
      const sim_interfaces::EngineInputs & inputs,
      const sim_msgs::msg::FlightModelState & fdm,
      const std::vector<std::string> & active_failures) override
  {
    // ── Read structured inputs ──────────────────────────────────────
    starter_engaged_ = inputs.starter[0];
    float mixture = inputs.mixture_norm[0];

    // Ignition maps to magneto: inputs.ignition[0] = magneto switch on
    // (node wrapper sets ignition from panel sel_magnetos > 0)
    bool ignition_on = inputs.ignition[0];

    // ── Read FDM engine data (passthrough) ──────────────────────────
    if (fdm.engine_count > 0) {
      state_.engine_rpm[0] = fdm.n1_rpm[0];
      state_.egt_degc[0] = fdm.engine_egt_degc[0];
      state_.oil_press_kpa[0] = fdm.engine_oil_pressure_psi[0] * PSI_TO_KPA;
      state_.oil_temp_degc[0] = fdm.engine_oil_temp_degc[0];
      state_.manifold_press_inhg[0] = fdm.engine_manifold_pressure_inhg[0];
      state_.fuel_flow_kgph[0] = fdm.fuel_flow_kgs[0] * 3600.0f;

      bool fdm_running = (fdm.engine_status_flags[0] & 0x01) != 0;
      engine_running_ = fdm_running;
    }

    // ── Cockpit logic ───────────────────────────────────────────────
    // Magneto OFF kills engine
    if (!ignition_on) {
      engine_running_ = false;
    }

    // Mixture at cutoff kills engine
    if (mixture < 0.01f) {
      engine_running_ = false;
    }

    // ── Engine state machine ────────────────────────────────────────
    // Starter needs bus voltage > 20V to engage (battery under load sags to ~18V
    // when flat — starter motor won't turn). This prevents starting without battery.
    bool bus_ok = inputs.bus_voltage > 20.0f;

    if (!engine_running_) {
      if (starter_engaged_ && ignition_on && mixture > 0.01f && inputs.fuel_available[0] && bus_ok) {
        state_.state[0] = sim_interfaces::EngineRunState::CRANKING;
      } else {
        state_.state[0] = sim_interfaces::EngineRunState::OFF;
      }
    } else {
      state_.state[0] = sim_interfaces::EngineRunState::RUNNING;
    }

    state_.starter_engaged[0] = starter_engaged_;

    // ── CHT thermal model ───────────────────────────────────────────
    float dt = static_cast<float>(dt_sec);
    if (engine_running_ && dt > 0.0f) {
      float power_frac = (rpm_max_ > 0.0f) ? (state_.engine_rpm[0] / rpm_max_) : 0.0f;
      float cht_target = 60.0f + power_frac * 160.0f;
      cht_ += (cht_target - cht_) * std::min(1.0f, 0.5f * dt);
    } else if (dt > 0.0f) {
      cht_ += (15.0f - cht_) * std::min(1.0f, 0.1f * dt);
    }
    state_.cht_degc[0] = cht_;

    // N1 as percentage of max RPM (piston convention)
    state_.n1_pct[0] = (rpm_max_ > 0.0f) ? (state_.engine_rpm[0] / rpm_max_ * 100.0f) : 0.0f;

    // Prop RPM = engine RPM for direct-drive piston (no reduction gearbox)
    state_.prop_rpm = state_.engine_rpm[0];

    // Turboshaft fields not applicable for piston
    state_.n2_pct[0] = 0.0f;
    state_.tot_degc[0] = 0.0f;
    state_.itt_degc[0] = 0.0f;
    state_.torque_nm[0] = 0.0f;
    state_.torque_pct[0] = 0.0f;
    state_.shp_kw[0] = 0.0f;

    state_.engine_count = engine_count_;

    // ── Failure overrides ───────────────────────────────────────────
    bool failed = false;
    for (const auto & f : active_failures) {
      if (f == "engine_0_oil_pressure") {
        state_.oil_press_kpa[0] = 0.0f;
      } else if (f == "engine_0_failure") {
        engine_running_ = false;
        state_.state[0] = sim_interfaces::EngineRunState::FAILED;
        failed = true;
      }
    }

    // ── Warning thresholds ──────────────────────────────────────────
    state_.low_oil_pressure_warning[0] =
        engine_running_ && state_.oil_press_kpa[0] < (oil_pressure_min_psi_ * PSI_TO_KPA);
    state_.high_egt_warning[0] = state_.egt_degc[0] > egt_max_degc_;
    state_.high_cht_warning[0] = state_.cht_degc[0] > cht_max_degc_;

    (void)failed;  // used implicitly via state enum
  }

  void set_running(uint8_t engine_index, bool running) override
  {
    if (engine_index == 0) {
      engine_running_ = running;
      state_.state[0] = running
        ? sim_interfaces::EngineRunState::RUNNING
        : sim_interfaces::EngineRunState::OFF;
    }
  }

  uint8_t get_engine_count() const override { return engine_count_; }

  void reset() override
  {
    state_ = {};
    state_.engine_count = engine_count_;
    engine_running_ = false;
    starter_engaged_ = false;
    cht_ = 15.0f;
  }

  sim_interfaces::EngineStateData get_state() const override { return state_; }

  sim_interfaces::EngineSwitchConfig get_switch_config() const override { return sw_cfg_; }

private:
  sim_interfaces::EngineStateData state_;
  uint8_t engine_count_ = 1;

  // YAML config thresholds
  float rpm_max_ = 2700.0f;
  float rpm_idle_ = 700.0f;
  float egt_max_degc_ = 900.0f;
  float cht_max_degc_ = 240.0f;
  float oil_pressure_min_psi_ = 25.0f;
  float oil_temp_max_degc_ = 118.0f;

  sim_interfaces::EngineSwitchConfig sw_cfg_;

  // Runtime state
  bool engine_running_ = false;
  bool starter_engaged_ = false;
  float cht_ = 15.0f;
};

}  // namespace aircraft_c172

PLUGINLIB_EXPORT_CLASS(aircraft_c172::EnginesModel, sim_interfaces::IEnginesModel)
