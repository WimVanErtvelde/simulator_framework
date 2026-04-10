#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <lifecycle_msgs/msg/state.hpp>
#include <std_msgs/msg/header.hpp>
#include <std_msgs/msg/string.hpp>
#include <pluginlib/class_loader.hpp>
#include <sim_interfaces/i_fuel_model.hpp>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/fuel_state.hpp>
#include <sim_msgs/msg/sim_state.hpp>
#include <sim_msgs/msg/sim_alert.hpp>
#include <sim_msgs/msg/panel_controls.hpp>
#include <sim_msgs/msg/initial_conditions.hpp>
#include <sim_msgs/msg/payload_command.hpp>
#include <sim_msgs/msg/flight_model_capabilities.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include <yaml-cpp/yaml.h>
#include <fstream>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class FuelNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  FuelNode()
  : LifecycleNode("sim_fuel", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    this->declare_parameter<std::string>("aircraft_id", "c172");
    this->declare_parameter<double>("update_rate_hz", 50.0);

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
    RCLCPP_INFO(this->get_logger(), "sim_fuel constructed (unconfigured)");
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle", 10);
    alert_pub_ = this->create_publisher<sim_msgs::msg::SimAlert>(
      "/sim/alerts", 10);
    fuel_state_pub_ = this->create_publisher<sim_msgs::msg::FuelState>(
      "/aircraft/fuel/state", 10);

    // Subscriptions
    flight_model_sub_ = this->create_subscription<sim_msgs::msg::FlightModelState>(
      "/aircraft/fdm/state", 10,
      [this](sim_msgs::msg::FlightModelState::SharedPtr msg) {
        latest_flight_model_state_ = *msg;
        flight_model_received_ = true;
      });

    sim_state_sub_ = this->create_subscription<sim_msgs::msg::SimState>(
      "/sim/state", 10,
      [this](sim_msgs::msg::SimState::SharedPtr msg) {
        auto prev = sim_state_;
        sim_state_ = msg->state;
        is_frozen_ = (msg->state == sim_msgs::msg::SimState::STATE_FROZEN);
        freeze_fuel_ = msg->freeze_fuel;

        if (sim_state_ == sim_msgs::msg::SimState::STATE_RESETTING &&
            prev != sim_msgs::msg::SimState::STATE_RESETTING) {
          if (model_) {
            model_->reset();
            RCLCPP_INFO(this->get_logger(), "Fuel reset to initial conditions");
          }
        }
      });

    panel_sub_ = this->create_subscription<sim_msgs::msg::PanelControls>(
      "/aircraft/controls/panel", 10,
      [this](sim_msgs::msg::PanelControls::SharedPtr msg) {
        latest_panel_ = *msg;
      });

    ic_sub_ = this->create_subscription<sim_msgs::msg::InitialConditions>(
      "/sim/initial_conditions", 10,
      [this](sim_msgs::msg::InitialConditions::SharedPtr msg) {
        if (model_) {
          model_->apply_initial_conditions(msg->fuel_total_norm);
          RCLCPP_INFO(this->get_logger(), "Applied IC: fuel_total_norm=%.2f", msg->fuel_total_norm);
        }
      });

    // Fuel load command from IOS (per-tank quantity set)
    fuel_load_sub_ = this->create_subscription<sim_msgs::msg::PayloadCommand>(
      "/aircraft/fuel/load_command", 10,
      [this](const sim_msgs::msg::PayloadCommand::SharedPtr msg) {
        if (!model_) return;
        for (size_t i = 0; i < msg->station_indices.size()
             && i < msg->weights_lbs.size(); ++i) {
          int tank_idx = msg->station_indices[i];
          double kg = msg->weights_lbs[i] * 0.453592;
          model_->set_tank_quantity(tank_idx, kg);
        }
        RCLCPP_INFO(this->get_logger(), "Fuel load: %zu tanks updated",
          msg->station_indices.size());
      });

    // Capabilities subscription (transient_local to receive latched message)
    auto caps_qos = rclcpp::QoS(1).transient_local().reliable();
    caps_sub_ = this->create_subscription<sim_msgs::msg::FlightModelCapabilities>(
      "/aircraft/fdm/capabilities", caps_qos,
      [this](const sim_msgs::msg::FlightModelCapabilities::SharedPtr msg) {
        latest_caps_ = msg;
        RCLCPP_INFO(this->get_logger(), "Received FDM capabilities: fuel_quantities=%u", msg->fuel_quantities);
      });

    // Writeback publisher — used when fuel mode is EXTERNAL_COUPLED
    fuel_writeback_pub_ = this->create_publisher<sim_msgs::msg::FuelState>(
      "/aircraft/writeback/fuel", 10);

    // Load aircraft-specific plugin + YAML config
    auto aircraft_id = this->get_parameter("aircraft_id").as_string();
    std::string plugin_name = "aircraft_" + aircraft_id + "::FuelModel";
    std::string yaml_path =
      ament_index_cpp::get_package_share_directory("aircraft_" + aircraft_id) +
      "/config/fuel.yaml";

    bool is_reload = (model_ != nullptr);
    if (is_reload) {
      RCLCPP_INFO(this->get_logger(),
        "Reloading fuel config from: %s", yaml_path.c_str());
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

      loader_ = std::make_unique<pluginlib::ClassLoader<sim_interfaces::IFuelModel>>(
        "sim_interfaces", "sim_interfaces::IFuelModel");
      model_ = loader_->createSharedInstance(plugin_name);
      model_->configure(yaml_path);

      // Read fuel config for derived display fields
      auto root = YAML::LoadFile(yaml_path);
      auto fuel = root["fuel"];
      density_kg_per_liter_ = fuel["density_kg_per_liter"].as<float>(0.72f);
      fuel_type_ = fuel["fuel_type"].as<std::string>("AVGAS_100LL");
      auto tanks_node = fuel["tanks"];
      tank_count_ = static_cast<uint8_t>(tanks_node.size());

    } catch (const std::exception & e) {
      std::string err = std::string("Failed to configure fuel: ") + e.what();
      RCLCPP_ERROR(this->get_logger(), "%s", err.c_str());
      publish_alert(sim_msgs::msg::SimAlert::SEVERITY_CRITICAL, err);
      model_.reset();
      loader_.reset();
      publish_lifecycle_state("unconfigured");
      return CallbackReturn::FAILURE;
    }

    RCLCPP_INFO(this->get_logger(), "sim_fuel configured — plugin: %s, config: %s",
      plugin_name.c_str(), yaml_path.c_str());
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    fuel_state_pub_->on_activate();

    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::String();
        msg.data = this->get_name();
        heartbeat_pub_->publish(msg);
      });

    double rate_hz = this->get_parameter("update_rate_hz").as_double();
    int period_ms = static_cast<int>(1000.0 / rate_hz);
    double dt_sec = 1.0 / rate_hz;

    update_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(period_ms),
      [this, dt_sec]() {
        if (is_frozen_ || freeze_fuel_) {
          auto state = model_->get_state();
          overlay_config_fields(state);
          state.header.stamp = this->now();
          fuel_state_pub_->publish(state);
          return;
        }

        // Build engine fuel flow vector from FDM state
        std::vector<float> engine_flows;
        if (flight_model_received_) {
          engine_flows.assign(
            latest_flight_model_state_.fuel_flow_kgs.begin(),
            latest_flight_model_state_.fuel_flow_kgs.end());
        }

        static const std::vector<std::string> no_failures;
        model_->update(dt_sec, engine_flows, latest_panel_, no_failures);

        auto state = model_->get_state();
        overlay_config_fields(state);
        state.header.stamp = this->now();
        fuel_state_pub_->publish(state);

        // Writeback to FDM when EXTERNAL_COUPLED
        if (latest_caps_ &&
            latest_caps_->fuel_quantities == sim_msgs::msg::FlightModelCapabilities::EXTERNAL_COUPLED) {
          // Starvation writeback: if fuel_pressure is zero for any engine,
          // zero all tanks so JSBSim's engine naturally quits from empty tanks.
          // Uses solver reachability (pressure), not engine demand — avoids
          // oscillation where dead engine has zero flow → demand check passes
          // → tanks restored → engine restarts → demand resumes → starved again.
          auto wb = state;
          // Starvation writeback: check engine 0 pressure only. The solver plugin
          // sets fuel_pressure_pa[i] = 35000 if fed, 0 if starved. Unused engine
          // slots (1-3 on single-engine) default to 0 and must NOT trigger this.
          // TODO: multi-engine support — check all engines up to actual engine_count
          bool engine_starved = (state.tank_count > 0 && state.fuel_pressure_pa[0] <= 0.0f);
          if (engine_starved) {
            for (int i = 0; i < 8; ++i) {
              wb.tank_quantity_kg[i] = 0.0f;
              wb.tank_usable_kg[i] = 0.0f;
              wb.tank_quantity_liters[i] = 0.0f;
            }
            wb.total_fuel_kg = 0.0f;
            wb.total_fuel_liters = 0.0f;
          }
          fuel_writeback_pub_->publish(wb);
        }
      });

    RCLCPP_INFO(this->get_logger(), "sim_fuel activated — %.0f Hz", rate_hz);
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    fuel_state_pub_->on_deactivate();
    heartbeat_timer_.reset();
    update_timer_.reset();
    RCLCPP_INFO(this->get_logger(), "sim_fuel deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    heartbeat_pub_.reset();
    lifecycle_state_pub_.reset();
    alert_pub_.reset();
    fuel_state_pub_.reset();
    flight_model_sub_.reset();
    sim_state_sub_.reset();
    panel_sub_.reset();
    ic_sub_.reset();
    fuel_load_sub_.reset();
    caps_sub_.reset();
    fuel_writeback_pub_.reset();
    latest_caps_.reset();
    model_.reset();
    loader_.reset();
    RCLCPP_INFO(this->get_logger(), "sim_fuel cleaned up");
    publish_lifecycle_state("unconfigured");
    return CallbackReturn::SUCCESS;
  }

private:
  void overlay_config_fields(sim_msgs::msg::FuelState & state)
  {
    uint8_t effective_tank_count = tank_count_;
    // FDM models fuel natively if no capabilities received or fuel_quantities==FDM_NATIVE
    bool fdm_models_fuel = flight_model_received_ && (!latest_caps_ ||
      latest_caps_->fuel_quantities == sim_msgs::msg::FlightModelCapabilities::FDM_NATIVE);

    if (fdm_models_fuel) {
      effective_tank_count = latest_flight_model_state_.fuel_tank_count;
    }

    state.tank_count = effective_tank_count;
    state.density_kg_per_liter = density_kg_per_liter_;
    state.fuel_type = fuel_type_;

    float inv_density = (density_kg_per_liter_ > 0.0f) ? 1.0f / density_kg_per_liter_ : 0.0f;

    // Per-tank liters, pct, and flow (only valid slots, rest zeroed)
    for (int i = 0; i < 8; ++i) {
      if (i < effective_tank_count) {
        state.tank_quantity_liters[i] = state.tank_quantity_kg[i] * inv_density;
        if (fdm_models_fuel) {
          float cap_kg = latest_flight_model_state_.fuel_tank_capacity_kg[i];
          state.tank_quantity_norm[i] = (cap_kg > 0.0f)
            ? state.tank_quantity_kg[i] / cap_kg : 0.0f;
        }
      } else {
        state.tank_quantity_liters[i] = 0.0f;
      }
      state.tank_flow_out_lph[i] = 0.0f;
    }
    state.total_fuel_liters = state.total_fuel_kg * inv_density;

    // Fuel flow: kg/s -> liters/hour
    for (int i = 0; i < 4; ++i) {
      state.engine_fuel_flow_lph[i] = state.engine_fuel_flow_kgs[i] * inv_density * 3600.0f;
    }
  }

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
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::FuelState>::SharedPtr fuel_state_pub_;

  // Subscriptions
  rclcpp::Subscription<sim_msgs::msg::FlightModelState>::SharedPtr flight_model_sub_;
  rclcpp::Subscription<sim_msgs::msg::SimState>::SharedPtr sim_state_sub_;
  rclcpp::Subscription<sim_msgs::msg::PanelControls>::SharedPtr panel_sub_;
  rclcpp::Subscription<sim_msgs::msg::InitialConditions>::SharedPtr ic_sub_;
  rclcpp::Subscription<sim_msgs::msg::PayloadCommand>::SharedPtr fuel_load_sub_;
  rclcpp::Subscription<sim_msgs::msg::FlightModelCapabilities>::SharedPtr caps_sub_;

  // Capabilities writeback
  rclcpp::Publisher<sim_msgs::msg::FuelState>::SharedPtr fuel_writeback_pub_;
  sim_msgs::msg::FlightModelCapabilities::SharedPtr latest_caps_;

  // Timers
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;
  rclcpp::TimerBase::SharedPtr update_timer_;

  // Plugin
  std::unique_ptr<pluginlib::ClassLoader<sim_interfaces::IFuelModel>> loader_;
  std::shared_ptr<sim_interfaces::IFuelModel> model_;

  // Cached state
  sim_msgs::msg::FlightModelState latest_flight_model_state_;
  sim_msgs::msg::PanelControls latest_panel_;
  bool flight_model_received_ = false;
  uint8_t sim_state_ = sim_msgs::msg::SimState::STATE_INIT;
  bool is_frozen_ = false;
  bool freeze_fuel_ = false;

  // Fuel config (from YAML, constant after configure)
  float density_kg_per_liter_ = 0.72f;
  std::string fuel_type_ = "AVGAS_100LL";
  uint8_t tank_count_ = 0;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<FuelNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
