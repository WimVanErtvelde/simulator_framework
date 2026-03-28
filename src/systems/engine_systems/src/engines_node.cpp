#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <lifecycle_msgs/msg/state.hpp>
#include <std_msgs/msg/string.hpp>
#include <pluginlib/class_loader.hpp>
#include <sim_interfaces/i_engines_model.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include <sim_msgs/msg/engine_state.hpp>
#include <sim_msgs/msg/engine_commands.hpp>
#include <sim_msgs/msg/sim_state.hpp>
#include <sim_msgs/msg/sim_alert.hpp>
#include <sim_msgs/msg/panel_controls.hpp>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/engine_controls.hpp>
#include <sim_msgs/msg/electrical_state.hpp>
#include <sim_msgs/msg/fuel_state.hpp>

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <set>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class EnginesNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  EnginesNode()
  : LifecycleNode("sim_engine_systems", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    this->declare_parameter<std::string>("aircraft_id", "c172");

    auto_start_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      [this]() {
        auto_start_timer_->cancel();
        auto_start_timer_.reset();
        auto st = this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE);
        if (st.id() != lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE) {
          RCLCPP_ERROR(this->get_logger(), "Auto-start: configure failed — stays unconfigured");
          return;
        }
        this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE);
      });
    RCLCPP_INFO(this->get_logger(), "sim_engine_systems constructed (unconfigured)");
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    // Publishers — create alert pub early so we can report config errors
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle_state", 10);
    alert_pub_ = this->create_publisher<sim_msgs::msg::SimAlert>(
      "/sim/alerts", 10);
    engine_state_pub_ = this->create_publisher<sim_msgs::msg::EngineState>(
      "/sim/engines/state", 10);
    engine_commands_pub_ = this->create_publisher<sim_msgs::msg::EngineCommands>(
      "/sim/engines/commands", 10);

    // Subscriptions
    sim_state_sub_ = this->create_subscription<sim_msgs::msg::SimState>(
      "/sim/state", 10,
      [this](const sim_msgs::msg::SimState::SharedPtr msg) {
        auto prev = sim_state_;
        sim_state_ = msg->state;

        if (sim_state_ == sim_msgs::msg::SimState::STATE_RESETTING &&
            prev != sim_msgs::msg::SimState::STATE_RESETTING) {
          if (model_) {
            model_->reset();
            RCLCPP_INFO(this->get_logger(), "Engine systems reset to initial conditions");
          }
        }
      });

    flight_model_sub_ = this->create_subscription<sim_msgs::msg::FlightModelState>(
      "/sim/flight_model/state", 10,
      [this](const sim_msgs::msg::FlightModelState::SharedPtr msg) {
        latest_fdm_ = *msg;
        fdm_received_ = true;
      });

    panel_sub_ = this->create_subscription<sim_msgs::msg::PanelControls>(
      "/sim/controls/panel", 10,
      [this](const sim_msgs::msg::PanelControls::SharedPtr msg) {
        latest_panel_ = *msg;
        panel_dirty_ = true;
      });

    engine_controls_sub_ = this->create_subscription<sim_msgs::msg::EngineControls>(
      "/sim/controls/engine", 10,
      [this](const sim_msgs::msg::EngineControls::SharedPtr msg) {
        latest_engine_controls_ = *msg;
      });

    // Electrical state for bus_voltage coupling
    electrical_sub_ = this->create_subscription<sim_msgs::msg::ElectricalState>(
      "/sim/electrical/state", 10,
      [this](const sim_msgs::msg::ElectricalState::SharedPtr msg) {
        latest_bus_voltage_ = msg->master_bus_voltage_v;
      });

    // Fuel state for fuel_available coupling
    fuel_sub_ = this->create_subscription<sim_msgs::msg::FuelState>(
      "/sim/fuel/state", 10,
      [this](const sim_msgs::msg::FuelState::SharedPtr msg) {
        // Fuel is available per engine if total fuel > 0 and pressure is adequate
        for (int i = 0; i < 4; ++i) {
          fuel_available_[i] = (msg->total_fuel_kg > 0.1f);
        }
      });

    // Load aircraft-specific plugin + YAML config
    auto aircraft_id = this->get_parameter("aircraft_id").as_string();
    std::string plugin_name = "aircraft_" + aircraft_id + "::EnginesModel";
    std::string yaml_path =
      ament_index_cpp::get_package_share_directory("aircraft_" + aircraft_id) +
      "/config/engine.yaml";

    bool is_reload = (model_ != nullptr);
    if (is_reload) {
      RCLCPP_INFO(this->get_logger(),
        "Reloading engine_systems config from: %s", yaml_path.c_str());
    }

    try {
      {
        std::ifstream test(yaml_path);
        if (!test.good()) {
          std::string err = "Config file not found: " + yaml_path;
          RCLCPP_ERROR(this->get_logger(), "%s", err.c_str());
          publish_alert(sim_msgs::msg::SimAlert::SEVERITY_CRITICAL, err);
          publish_lifecycle_state("unconfigured");
          return CallbackReturn::FAILURE;
        }
      }

      loader_ = std::make_unique<pluginlib::ClassLoader<sim_interfaces::IEnginesModel>>(
        "sim_interfaces", "sim_interfaces::IEnginesModel");
      model_ = loader_->createSharedInstance(plugin_name);
      model_->configure(yaml_path);
      sw_cfg_ = model_->get_switch_config();

      // Read engine_type from YAML for logging and msg field
      auto root = YAML::LoadFile(yaml_path);
      engine_type_ = root["engine_type"] ? root["engine_type"].as<std::string>("unknown") : "unknown";
      RCLCPP_INFO(this->get_logger(), "Engine type: %s, engine_count: %u",
        engine_type_.c_str(), model_->get_engine_count());

    } catch (const std::exception & e) {
      std::string err = std::string("Failed to configure engine_systems: ") + e.what();
      RCLCPP_ERROR(this->get_logger(), "%s", err.c_str());
      publish_alert(sim_msgs::msg::SimAlert::SEVERITY_CRITICAL, err);
      model_.reset();
      loader_.reset();
      publish_lifecycle_state("unconfigured");
      return CallbackReturn::FAILURE;
    }

    RCLCPP_INFO(this->get_logger(), "sim_engine_systems configured — plugin: %s, config: %s",
      plugin_name.c_str(), yaml_path.c_str());
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    engine_state_pub_->on_activate();

    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::String();
        msg.data = this->get_name();
        heartbeat_pub_->publish(msg);
      });

    // 50 Hz update timer (20ms)
    constexpr double dt_sec = 0.02;
    update_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(20),
      [this, dt_sec]() {
        if (!model_) return;

        auto inputs = build_engine_inputs();

        bool running = (sim_state_ == sim_msgs::msg::SimState::STATE_INIT ||
                        sim_state_ == sim_msgs::msg::SimState::STATE_READY ||
                        sim_state_ == sim_msgs::msg::SimState::STATE_RUNNING);

        if (running) {
          static const std::vector<std::string> no_failures;
          model_->update(dt_sec, inputs, latest_fdm_, no_failures);
        } else if (panel_dirty_) {
          // Frozen but panel changed — run once with dt=0 (no time advance)
          static const std::vector<std::string> no_failures;
          model_->update(0.0, inputs, latest_fdm_, no_failures);
          panel_dirty_ = false;
        }

        // Always publish — IOS needs live data even when frozen
        auto snap = model_->get_state();
        publish_engine_state(snap);
        publish_engine_commands(snap.engine_count);
      });

    RCLCPP_INFO(this->get_logger(), "sim_engine_systems activated");
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    heartbeat_timer_.reset();
    update_timer_.reset();
    engine_state_pub_->on_deactivate();
    RCLCPP_INFO(this->get_logger(), "sim_engine_systems deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    heartbeat_pub_.reset();
    lifecycle_state_pub_.reset();
    alert_pub_.reset();
    engine_state_pub_.reset();
    engine_commands_pub_.reset();
    sim_state_sub_.reset();
    flight_model_sub_.reset();
    panel_sub_.reset();
    engine_controls_sub_.reset();
    electrical_sub_.reset();
    fuel_sub_.reset();
    model_.reset();
    loader_.reset();
    sw_cfg_ = {};
    RCLCPP_INFO(this->get_logger(), "sim_engine_systems cleaned up");
    publish_lifecycle_state("unconfigured");
    return CallbackReturn::SUCCESS;
  }

private:
  void publish_lifecycle_state(const std::string & state)
  {
    if (lifecycle_state_pub_) {
      auto msg = std_msgs::msg::String();
      msg.data = std::string(this->get_name()) + ":" + state;
      lifecycle_state_pub_->publish(msg);
    }
  }

  void publish_alert(uint8_t severity, const std::string & message)
  {
    if (alert_pub_) {
      auto msg = sim_msgs::msg::SimAlert();
      msg.header.stamp = this->now();
      msg.severity = severity;
      msg.source = this->get_name();
      msg.message = message;
      alert_pub_->publish(msg);
    }
  }

  /// Assemble EngineInputs from arbitrated controls + systems coupling
  sim_interfaces::EngineInputs build_engine_inputs()
  {
    sim_interfaces::EngineInputs inputs;

    // Throttle/mixture from arbitrated engine controls
    auto & ec = latest_engine_controls_;
    for (size_t i = 0; i < ec.throttle_norm.size() && i < 4; ++i) {
      inputs.throttle_norm[i] = ec.throttle_norm[i];
    }
    for (size_t i = 0; i < ec.mixture_norm.size() && i < 4; ++i) {
      inputs.mixture_norm[i] = ec.mixture_norm[i];
    }

    // Panel switches → discrete inputs (IDs from aircraft YAML via plugin)
    for (size_t i = 0; i < latest_panel_.switch_ids.size() &&
                        i < latest_panel_.switch_states.size(); ++i) {
      const auto & id = latest_panel_.switch_ids[i];
      bool on = latest_panel_.switch_states[i];

      for (size_t e = 0; e < sw_cfg_.starter_ids.size() && e < 4; ++e) {
        if (!sw_cfg_.starter_ids[e].empty() && id == sw_cfg_.starter_ids[e])
          inputs.starter[e] = on;
      }
      for (size_t e = 0; e < sw_cfg_.ignition_ids.size() && e < 4; ++e) {
        if (!sw_cfg_.ignition_ids[e].empty() && id == sw_cfg_.ignition_ids[e])
          inputs.ignition[e] = on;
      }
      for (size_t e = 0; e < sw_cfg_.fuel_cutoff_ids.size() && e < 4; ++e) {
        if (!sw_cfg_.fuel_cutoff_ids[e].empty() && id == sw_cfg_.fuel_cutoff_ids[e])
          inputs.fuel_cutoff[e] = on;
      }
    }

    // Selectors → power/prop/condition levers + magneto/ignition (IDs from aircraft YAML)
    for (size_t i = 0; i < latest_panel_.selector_ids.size() &&
                        i < latest_panel_.selector_values.size(); ++i) {
      const auto & id = latest_panel_.selector_ids[i];
      int val = latest_panel_.selector_values[i];
      float norm = static_cast<float>(val) / 100.0f;  // selector 0-100 → 0.0-1.0

      for (size_t e = 0; e < sw_cfg_.prop_lever_ids.size() && e < 4; ++e) {
        if (!sw_cfg_.prop_lever_ids[e].empty() && id == sw_cfg_.prop_lever_ids[e])
          inputs.prop_lever_norm[e] = norm;
      }
      for (size_t e = 0; e < sw_cfg_.condition_lever_ids.size() && e < 4; ++e) {
        if (!sw_cfg_.condition_lever_ids[e].empty() && id == sw_cfg_.condition_lever_ids[e])
          inputs.condition_lever_norm[e] = norm;
      }
      for (size_t e = 0; e < sw_cfg_.power_lever_ids.size() && e < 4; ++e) {
        if (!sw_cfg_.power_lever_ids[e].empty() && id == sw_cfg_.power_lever_ids[e])
          inputs.power_lever_norm[e] = norm;
      }
      // Magneto selector: 0=OFF, 1=R, 2=L, 3=BOTH → ignition on if > 0
      // Position 4 = START → also triggers starter
      for (size_t e = 0; e < sw_cfg_.ignition_ids.size() && e < 4; ++e) {
        if (!sw_cfg_.ignition_ids[e].empty() && id == sw_cfg_.ignition_ids[e]) {
          inputs.ignition[e] = (val >= 1 && val <= 4);
          if (val == 4) inputs.starter[e] = true;  // START position
        }
      }
    }

    // Systems coupling
    inputs.bus_voltage = latest_bus_voltage_;
    inputs.fuel_available = fuel_available_;

    return inputs;
  }

  void publish_engine_state(const sim_interfaces::EngineStateData & snap)
  {
    auto msg = sim_msgs::msg::EngineState();
    msg.header.stamp = this->now();
    msg.engine_count = snap.engine_count;
    msg.engine_type = engine_type_;

    for (int i = 0; i < 4; ++i) {
      msg.engine_state[i] = static_cast<uint8_t>(snap.state[i]);
      msg.n1_pct[i] = snap.n1_pct[i];
      msg.n2_pct[i] = snap.n2_pct[i];
      msg.engine_rpm[i] = snap.engine_rpm[i];
      msg.itt_degc[i] = snap.itt_degc[i];
      msg.egt_degc[i] = snap.egt_degc[i];
      msg.tot_degc[i] = snap.tot_degc[i];
      msg.cht_degc[i] = snap.cht_degc[i];
      msg.oil_press_kpa[i] = snap.oil_press_kpa[i];
      msg.oil_temp_degc[i] = snap.oil_temp_degc[i];
      msg.fuel_flow_kgph[i] = snap.fuel_flow_kgph[i];
      msg.manifold_press_inhg[i] = snap.manifold_press_inhg[i];
      msg.torque_nm[i] = snap.torque_nm[i];
      msg.torque_pct[i] = snap.torque_pct[i];
      msg.shp_kw[i] = snap.shp_kw[i];
      msg.starter_engaged[i] = snap.starter_engaged[i];
      msg.generator_online[i] = snap.generator_online[i];
      msg.low_oil_pressure_warning[i] = snap.low_oil_pressure_warning[i];
      msg.high_egt_warning[i] = snap.high_egt_warning[i];
      msg.high_cht_warning[i] = snap.high_cht_warning[i];
    }

    msg.prop_rpm = snap.prop_rpm;
    msg.main_rotor_rpm = snap.main_rotor_rpm;
    msg.tail_rotor_rpm = snap.tail_rotor_rpm;

    msg.fadec_mode_0 = snap.fadec_mode[0].empty() ? "OFF" : snap.fadec_mode[0];
    msg.fadec_mode_1 = snap.fadec_mode[1].empty() ? "OFF" : snap.fadec_mode[1];
    msg.fadec_mode_2 = snap.fadec_mode[2].empty() ? "OFF" : snap.fadec_mode[2];
    msg.fadec_mode_3 = snap.fadec_mode[3].empty() ? "OFF" : snap.fadec_mode[3];

    engine_state_pub_->publish(msg);
  }

  void publish_engine_commands(uint8_t engine_count)
  {
    auto msg = sim_msgs::msg::EngineCommands();
    msg.header.stamp = this->now();
    msg.engine_count = engine_count;
    // All fields default to zero/false — correct for piston/turboshaft.
    // Turboprop/FADEC plugins will populate these when implemented.
    engine_commands_pub_->publish(msg);
  }

  // Publishers
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr heartbeat_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr lifecycle_state_pub_;
  rclcpp::Publisher<sim_msgs::msg::SimAlert>::SharedPtr alert_pub_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::EngineState>::SharedPtr engine_state_pub_;
  rclcpp::Publisher<sim_msgs::msg::EngineCommands>::SharedPtr engine_commands_pub_;

  // Subscriptions
  rclcpp::Subscription<sim_msgs::msg::SimState>::SharedPtr sim_state_sub_;
  rclcpp::Subscription<sim_msgs::msg::FlightModelState>::SharedPtr flight_model_sub_;
  rclcpp::Subscription<sim_msgs::msg::PanelControls>::SharedPtr panel_sub_;
  rclcpp::Subscription<sim_msgs::msg::EngineControls>::SharedPtr engine_controls_sub_;
  rclcpp::Subscription<sim_msgs::msg::ElectricalState>::SharedPtr electrical_sub_;
  rclcpp::Subscription<sim_msgs::msg::FuelState>::SharedPtr fuel_sub_;

  // Timers
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;
  rclcpp::TimerBase::SharedPtr update_timer_;

  // Plugin
  std::unique_ptr<pluginlib::ClassLoader<sim_interfaces::IEnginesModel>> loader_;
  std::shared_ptr<sim_interfaces::IEnginesModel> model_;

  // State
  uint8_t sim_state_ = sim_msgs::msg::SimState::STATE_INIT;
  bool panel_dirty_ = false;
  bool fdm_received_ = false;
  sim_msgs::msg::FlightModelState latest_fdm_;
  sim_msgs::msg::PanelControls latest_panel_;
  sim_msgs::msg::EngineControls latest_engine_controls_;
  std::string engine_type_ = "unknown";
  sim_interfaces::EngineSwitchConfig sw_cfg_;

  // Systems coupling state
  float latest_bus_voltage_ = 0.0f;
  std::array<bool, 4> fuel_available_ = {};
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<EnginesNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
