#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <std_msgs/msg/string.hpp>
#include <pluginlib/class_loader.hpp>
#include <sim_interfaces/i_air_data_model.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include <sim_msgs/msg/air_data_state.hpp>
#include <sim_msgs/msg/sim_state.hpp>
#include <sim_msgs/msg/sim_alert.hpp>
#include <sim_msgs/msg/failure_injection.hpp>
#include <sim_msgs/msg/panel_controls.hpp>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/flight_model_capabilities.hpp>
#include <sim_msgs/msg/atmosphere_state.hpp>
#include <sim_msgs/msg/weather_state.hpp>
#include <sim_msgs/msg/electrical_state.hpp>

#include <fstream>
#include <set>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class AirDataNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  AirDataNode()
  : LifecycleNode("sim_air_data", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    this->declare_parameter<std::string>("aircraft_id", "c172");

    auto_start_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      [this]() {
        auto_start_timer_->cancel();
        auto_start_timer_.reset();
        this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE);
        this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE);
      });
    RCLCPP_INFO(this->get_logger(), "sim_air_data constructed (unconfigured)");
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    // Publishers — create alert pub early so config errors can be reported
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle_state", 10);
    alert_pub_ = this->create_publisher<sim_msgs::msg::SimAlert>(
      "/sim/alerts", 10);
    air_data_state_pub_ = this->create_publisher<sim_msgs::msg::AirDataState>(
      "/sim/air_data/state", 10);

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
            RCLCPP_INFO(this->get_logger(), "Air data reset to initial conditions");
          }
        }
      });

    flight_model_sub_ = this->create_subscription<sim_msgs::msg::FlightModelState>(
      "/sim/flight_model/state", 10,
      [this](const sim_msgs::msg::FlightModelState::SharedPtr msg) {
        latest_fms_ = msg;
      });

    atmosphere_sub_ = this->create_subscription<sim_msgs::msg::AtmosphereState>(
      "/sim/world/atmosphere", 10,
      [this](const sim_msgs::msg::AtmosphereState::SharedPtr msg) {
        latest_atmos_ = msg;
      });

    weather_sub_ = this->create_subscription<sim_msgs::msg::WeatherState>(
      "/sim/world/weather", 10,
      [this](const sim_msgs::msg::WeatherState::SharedPtr msg) {
        latest_weather_ = msg;
      });

    electrical_sub_ = this->create_subscription<sim_msgs::msg::ElectricalState>(
      "/sim/electrical/state", 10,
      [this](const sim_msgs::msg::ElectricalState::SharedPtr msg) {
        latest_electrical_ = msg;
      });

    panel_sub_ = this->create_subscription<sim_msgs::msg::PanelControls>(
      "/sim/controls/panel", 10,
      [this](const sim_msgs::msg::PanelControls::SharedPtr msg) {
        latest_panel_ = msg;
      });

    // Failure injection commands from sim_failures
    failure_injection_sub_ = this->create_subscription<sim_msgs::msg::FailureInjection>(
      "/sim/failure/air_data_commands",
      rclcpp::QoS(10).reliable(),
      [this](const sim_msgs::msg::FailureInjection::SharedPtr msg) {
        if (!model_) return;
        model_->apply_failure(msg->method, msg->active);
        RCLCPP_INFO(this->get_logger(), "Air data failure injection: %s %s",
          msg->method.c_str(), msg->active ? "active" : "cleared");
      });

    // Capabilities subscription (transient_local to receive latched message)
    auto caps_qos = rclcpp::QoS(1).transient_local().reliable();
    caps_sub_ = this->create_subscription<sim_msgs::msg::FlightModelCapabilities>(
      "/sim/flight_model/capabilities", caps_qos,
      [this](const sim_msgs::msg::FlightModelCapabilities::SharedPtr msg) {
        latest_caps_ = msg;
        RCLCPP_INFO(this->get_logger(), "Received FDM capabilities");
      });

    // Load aircraft-specific plugin + YAML config
    auto aircraft_id = this->get_parameter("aircraft_id").as_string();
    std::string plugin_name = "aircraft_" + aircraft_id + "::AirDataModel";
    std::string yaml_path =
      ament_index_cpp::get_package_share_directory("aircraft_" + aircraft_id) +
      "/config/air_data.yaml";

    bool is_reload = (model_ != nullptr);
    if (is_reload) {
      RCLCPP_INFO(this->get_logger(),
        "Reloading air_data config from: %s", yaml_path.c_str());
    }

    try {
      // Verify file exists before attempting plugin load
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

      loader_ = std::make_unique<pluginlib::ClassLoader<sim_interfaces::IAirDataModel>>(
        "sim_interfaces", "sim_interfaces::IAirDataModel");
      model_ = loader_->createSharedInstance(plugin_name);
      model_->configure(yaml_path);

      // Retrieve the load/switch names from the plugin for later lookup
      heat_load_names_ = model_->get_heat_load_names();
      alternate_switch_ids_ = model_->get_alternate_static_switch_ids();

    } catch (const std::exception & e) {
      std::string err = std::string("Failed to configure air_data: ") + e.what();
      RCLCPP_ERROR(this->get_logger(), "%s", err.c_str());
      publish_alert(sim_msgs::msg::SimAlert::SEVERITY_CRITICAL, err);
      model_.reset();
      loader_.reset();
      publish_lifecycle_state("unconfigured");
      return CallbackReturn::FAILURE;
    }

    RCLCPP_INFO(this->get_logger(), "sim_air_data configured — plugin: %s, config: %s",
      plugin_name.c_str(), yaml_path.c_str());
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    air_data_state_pub_->on_activate();

    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::String();
        msg.data = this->get_name();
        heartbeat_pub_->publish(msg);
      });

    // 50 Hz update timer (20 ms)
    constexpr double dt_sec = 0.02;
    update_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(20),
      [this, dt_sec]() {
        if (!model_) return;

        // air_data is always EXTERNAL_DECOUPLED — we always compute instrument values.
        // No FDM_NATIVE gating needed: no FDM provides a meaningful pitot-static failure model.

        bool running = (sim_state_ == sim_msgs::msg::SimState::STATE_INIT ||
                        sim_state_ == sim_msgs::msg::SimState::STATE_READY ||
                        sim_state_ == sim_msgs::msg::SimState::STATE_RUNNING);

        // Build inputs from cached messages
        sim_interfaces::AirDataInputs inputs;

        if (latest_fms_) {
          inputs.tas_ms           = latest_fms_->tas_ms;
          inputs.altitude_msl_m   = latest_fms_->altitude_msl_m;
          inputs.vertical_speed_ms = latest_fms_->vertical_speed_ms;
          inputs.alpha_rad        = latest_fms_->alpha_rad;
          // If atmosphere node hasn't published yet, fall back to FMS values
          inputs.static_pressure_pa = latest_fms_->static_pressure_pa;
          inputs.temperature_k    = latest_fms_->temperature_k;
          inputs.density_kgm3     = latest_fms_->air_density_kgm3;
          inputs.qnh_pa           = 101325.0;
        }

        if (latest_atmos_) {
          inputs.static_pressure_pa = latest_atmos_->pressure_pa;
          inputs.temperature_k    = latest_atmos_->oat_k;
          inputs.density_kgm3     = latest_atmos_->density_kgm3;
          inputs.qnh_pa           = latest_atmos_->qnh_pa;
        }

        if (latest_weather_) {
          inputs.turbulence_intensity = latest_weather_->turbulence_intensity;
          inputs.visible_moisture = latest_weather_->visible_moisture;
        }

        // Resolve pitot heat powered state from ElectricalState
        inputs.pitot_heat_powered.resize(heat_load_names_.size(), false);
        if (latest_electrical_) {
          for (size_t i = 0; i < heat_load_names_.size(); ++i) {
            const auto & name = heat_load_names_[i];
            for (size_t j = 0;
                 j < latest_electrical_->load_names.size() &&
                 j < latest_electrical_->load_powered.size(); ++j)
            {
              if (latest_electrical_->load_names[j] == name) {
                inputs.pitot_heat_powered[i] = latest_electrical_->load_powered[j];
                break;
              }
            }
          }
        }

        // Resolve alternate static switch from PanelControls
        inputs.alternate_static_selected.resize(alternate_switch_ids_.size(), false);
        if (latest_panel_) {
          for (size_t i = 0; i < alternate_switch_ids_.size(); ++i) {
            const auto & sw_id = alternate_switch_ids_[i];
            for (size_t j = 0;
                 j < latest_panel_->switch_ids.size() &&
                 j < latest_panel_->switch_states.size(); ++j)
            {
              if (latest_panel_->switch_ids[j] == sw_id) {
                inputs.alternate_static_selected[i] = latest_panel_->switch_states[j];
                break;
              }
            }
          }
        }

        if (running) {
          model_->update(dt_sec, inputs);
        }
        // FROZEN: hold current instrument values (frozen instruments — correct behaviour)
        // RESETTING: handled in sim_state subscriber above

        // Always publish — IOS and cockpit displays need live data even when frozen
        auto snap = model_->get_snapshot();
        auto msg = sim_msgs::msg::AirDataState();
        msg.header.stamp = this->now();

        msg.system_count = static_cast<uint8_t>(snap.systems.size());
        for (size_t i = 0; i < snap.systems.size() && i < 3; ++i) {
          const auto & s = snap.systems[i];
          msg.system_names[i]            = s.name;
          msg.indicated_airspeed_ms[i]   = s.indicated_airspeed_ms;
          msg.calibrated_airspeed_ms[i]  = s.calibrated_airspeed_ms;
          msg.mach[i]                    = s.mach;
          msg.altitude_indicated_m[i]    = s.altitude_indicated_m;
          msg.altitude_pressure_m[i]     = s.altitude_pressure_m;
          msg.vertical_speed_ms[i]       = s.vertical_speed_ms;
          msg.sat_k[i]                   = s.sat_k;
          msg.tat_k[i]                   = s.tat_k;
          msg.pitot_healthy[i]           = s.pitot_healthy;
          msg.static_healthy[i]          = s.static_healthy;
          msg.pitot_heat_on[i]           = s.pitot_heat_on;
          msg.alternate_static_active[i] = s.alternate_static_active;
          msg.pitot_ice_pct[i]           = s.pitot_ice_pct;
        }

        air_data_state_pub_->publish(msg);
      });

    RCLCPP_INFO(this->get_logger(), "sim_air_data activated");
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    heartbeat_timer_.reset();
    update_timer_.reset();
    RCLCPP_INFO(this->get_logger(), "sim_air_data deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    heartbeat_pub_.reset();
    lifecycle_state_pub_.reset();
    alert_pub_.reset();
    air_data_state_pub_.reset();
    sim_state_sub_.reset();
    flight_model_sub_.reset();
    atmosphere_sub_.reset();
    weather_sub_.reset();
    electrical_sub_.reset();
    panel_sub_.reset();
    failure_injection_sub_.reset();
    caps_sub_.reset();
    latest_fms_.reset();
    latest_atmos_.reset();
    latest_weather_.reset();
    latest_electrical_.reset();
    latest_panel_.reset();
    latest_caps_.reset();
    model_.reset();
    loader_.reset();
    heat_load_names_.clear();
    alternate_switch_ids_.clear();
    RCLCPP_INFO(this->get_logger(), "sim_air_data cleaned up");
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

  // Publishers
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr heartbeat_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr lifecycle_state_pub_;
  rclcpp::Publisher<sim_msgs::msg::SimAlert>::SharedPtr alert_pub_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::AirDataState>::SharedPtr air_data_state_pub_;

  // Subscriptions
  rclcpp::Subscription<sim_msgs::msg::SimState>::SharedPtr sim_state_sub_;
  rclcpp::Subscription<sim_msgs::msg::FlightModelState>::SharedPtr flight_model_sub_;
  rclcpp::Subscription<sim_msgs::msg::AtmosphereState>::SharedPtr atmosphere_sub_;
  rclcpp::Subscription<sim_msgs::msg::WeatherState>::SharedPtr weather_sub_;
  rclcpp::Subscription<sim_msgs::msg::ElectricalState>::SharedPtr electrical_sub_;
  rclcpp::Subscription<sim_msgs::msg::PanelControls>::SharedPtr panel_sub_;
  rclcpp::Subscription<sim_msgs::msg::FailureInjection>::SharedPtr failure_injection_sub_;
  rclcpp::Subscription<sim_msgs::msg::FlightModelCapabilities>::SharedPtr caps_sub_;

  // Timers
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;
  rclcpp::TimerBase::SharedPtr update_timer_;

  // Plugin
  std::unique_ptr<pluginlib::ClassLoader<sim_interfaces::IAirDataModel>> loader_;
  std::shared_ptr<sim_interfaces::IAirDataModel> model_;

  // Cached latest messages
  sim_msgs::msg::FlightModelState::SharedPtr latest_fms_;
  sim_msgs::msg::AtmosphereState::SharedPtr latest_atmos_;
  sim_msgs::msg::WeatherState::SharedPtr latest_weather_;
  sim_msgs::msg::ElectricalState::SharedPtr latest_electrical_;
  sim_msgs::msg::PanelControls::SharedPtr latest_panel_;
  sim_msgs::msg::FlightModelCapabilities::SharedPtr latest_caps_;

  // Names retrieved from plugin after configure()
  std::vector<std::string> heat_load_names_;
  std::vector<std::string> alternate_switch_ids_;

  // State
  uint8_t sim_state_ = sim_msgs::msg::SimState::STATE_INIT;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<AirDataNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
