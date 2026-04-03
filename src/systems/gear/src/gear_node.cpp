#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <lifecycle_msgs/msg/state.hpp>
#include <std_msgs/msg/string.hpp>
#include <pluginlib/class_loader.hpp>
#include <sim_interfaces/i_gear_model.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include <sim_msgs/msg/gear_state.hpp>
#include <sim_msgs/msg/sim_state.hpp>
#include <sim_msgs/msg/sim_alert.hpp>
#include <sim_msgs/msg/failure_injection.hpp>
#include <sim_msgs/msg/flight_controls.hpp>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/flight_model_capabilities.hpp>

#include <fstream>
#include <set>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class GearNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  GearNode()
  : LifecycleNode("sim_gear", rclcpp::NodeOptions().parameter_overrides(
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
    RCLCPP_INFO(this->get_logger(), "sim_gear constructed (unconfigured)");
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    // Publishers — create alert pub early so config errors can be reported
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle", 10);
    alert_pub_ = this->create_publisher<sim_msgs::msg::SimAlert>(
      "/sim/alerts", 10);
    gear_state_pub_ = this->create_publisher<sim_msgs::msg::GearState>(
      "/aircraft/gear/state", 10);

    // Subscriptions
    sim_state_sub_ = this->create_subscription<sim_msgs::msg::SimState>(
      "/sim/state", 10,
      [this](const sim_msgs::msg::SimState::SharedPtr msg) {
        auto prev = sim_state_;
        sim_state_ = msg->state;

        // Detect RESETTING transition → reset model to initial conditions
        if (sim_state_ == sim_msgs::msg::SimState::STATE_RESETTING &&
            prev != sim_msgs::msg::SimState::STATE_RESETTING) {
          if (model_) {
            model_->reset();
            RCLCPP_INFO(this->get_logger(), "Gear reset to initial conditions");
          }
        }
      });

    flight_model_sub_ = this->create_subscription<sim_msgs::msg::FlightModelState>(
      "/aircraft/fdm/state", 10,
      [this](const sim_msgs::msg::FlightModelState::SharedPtr msg) {
        latest_fms_ = msg;
      });

    flight_controls_sub_ = this->create_subscription<sim_msgs::msg::FlightControls>(
      "/aircraft/controls/flight", 10,
      [this](const sim_msgs::msg::FlightControls::SharedPtr msg) {
        latest_controls_ = msg;
      });

    // Failure injection commands from sim_failures
    failure_injection_sub_ = this->create_subscription<sim_msgs::msg::FailureInjection>(
      "/sim/failures/route/gear",
      rclcpp::QoS(10).reliable(),
      [this](const sim_msgs::msg::FailureInjection::SharedPtr msg) {
        if (!model_) return;
        model_->apply_failure(msg->method, msg->active);
        RCLCPP_INFO(this->get_logger(), "Gear failure injection: %s %s",
          msg->method.c_str(), msg->active ? "active" : "cleared");
      });

    // Capabilities subscription (transient_local to receive latched message)
    auto caps_qos = rclcpp::QoS(1).transient_local().reliable();
    caps_sub_ = this->create_subscription<sim_msgs::msg::FlightModelCapabilities>(
      "/aircraft/fdm/capabilities", caps_qos,
      [this](const sim_msgs::msg::FlightModelCapabilities::SharedPtr msg) {
        latest_caps_ = msg;
        RCLCPP_INFO(this->get_logger(), "Received FDM capabilities: gear_retract=%u",
          msg->gear_retract);
      });

    // Load aircraft-specific plugin + YAML config
    auto aircraft_id = this->get_parameter("aircraft_id").as_string();
    std::string plugin_name = "aircraft_" + aircraft_id + "::GearModel";
    std::string yaml_path =
      ament_index_cpp::get_package_share_directory("aircraft_" + aircraft_id) +
      "/config/gear.yaml";

    bool is_reload = (model_ != nullptr);
    if (is_reload) {
      RCLCPP_INFO(this->get_logger(),
        "Reloading gear config from: %s", yaml_path.c_str());
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

      loader_ = std::make_unique<pluginlib::ClassLoader<sim_interfaces::IGearModel>>(
        "sim_interfaces", "sim_interfaces::IGearModel");
      model_ = loader_->createSharedInstance(plugin_name);
      model_->configure(yaml_path);

    } catch (const std::exception & e) {
      std::string err = std::string("Failed to configure gear: ") + e.what();
      RCLCPP_ERROR(this->get_logger(), "%s", err.c_str());
      publish_alert(sim_msgs::msg::SimAlert::SEVERITY_CRITICAL, err);
      model_.reset();
      loader_.reset();
      publish_lifecycle_state("unconfigured");
      return CallbackReturn::FAILURE;
    }

    RCLCPP_INFO(this->get_logger(), "sim_gear configured — plugin: %s, config: %s",
      plugin_name.c_str(), yaml_path.c_str());
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    gear_state_pub_->on_activate();

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

        // Gate: if FDM handles gear natively, don't run our solver
        bool mode_is_ours = !latest_caps_ ||
          latest_caps_->gear_retract != sim_msgs::msg::FlightModelCapabilities::FDM_NATIVE;
        if (!mode_is_ours) return;

        bool running = (sim_state_ == sim_msgs::msg::SimState::STATE_INIT ||
                        sim_state_ == sim_msgs::msg::SimState::STATE_READY ||
                        sim_state_ == sim_msgs::msg::SimState::STATE_RUNNING);

        // Extract gear data from latest FlightModelState
        std::vector<bool>  wow_vec;
        std::vector<float> pos_vec;
        std::vector<float> steer_vec;
        bool on_ground = false;
        uint8_t gear_count = 0;
        bool gear_handle_down = true;  // default: extended
        float brake_left = 0.0f;
        float brake_right = 0.0f;
        bool parking_brake = false;

        if (latest_fms_) {
          gear_count = latest_fms_->gear_count;
          on_ground  = latest_fms_->on_ground;
          for (uint8_t i = 0; i < gear_count && i < 5; ++i) {
            wow_vec.push_back(latest_fms_->gear_on_ground[i]);
            pos_vec.push_back(latest_fms_->gear_position_norm[i]);
            steer_vec.push_back(latest_fms_->wheel_angle_deg[i]);
          }
        }

        if (latest_controls_) {
          gear_handle_down = latest_controls_->gear_down;
          brake_left       = latest_controls_->brake_left_norm;
          brake_right      = latest_controls_->brake_right_norm;
          parking_brake    = latest_controls_->parking_brake;
        }

        if (running) {
          model_->update(dt_sec, wow_vec, pos_vec, steer_vec, gear_handle_down, on_ground);
        }
        // Always publish — IOS needs live data even when frozen

        auto snap = model_->get_snapshot();
        auto msg = sim_msgs::msg::GearState();
        msg.header.stamp = this->now();

        // Configuration fields
        msg.gear_type  = snap.gear_type;
        msg.retractable = snap.retractable;
        msg.gear_count  = static_cast<uint8_t>(snap.legs.size());

        // Per-leg state
        for (size_t i = 0; i < snap.legs.size() && i < 5; ++i) {
          msg.leg_names[i]        = snap.legs[i].name;
          msg.position_norm[i]     = snap.legs[i].position_norm;
          msg.weight_on_wheels[i] = snap.legs[i].weight_on_wheels;
          msg.status[i]           = snap.legs[i].status;
        }

        // Aggregate state
        msg.on_ground        = snap.on_ground;
        msg.gear_handle_down = gear_handle_down;
        msg.gear_unsafe      = snap.gear_unsafe;
        msg.gear_warning     = snap.gear_warning;

        // Brakes (echoed from FlightControls for single-source display)
        msg.brake_left_norm   = brake_left;
        msg.brake_right_norm  = brake_right;
        msg.parking_brake = parking_brake;

        // Nosewheel
        msg.nosewheel_angle_deg = snap.nosewheel_angle_deg;

        gear_state_pub_->publish(msg);
      });

    RCLCPP_INFO(this->get_logger(), "sim_gear activated");
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    heartbeat_timer_.reset();
    update_timer_.reset();
    gear_state_pub_->on_deactivate();
    RCLCPP_INFO(this->get_logger(), "sim_gear deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    heartbeat_pub_.reset();
    lifecycle_state_pub_.reset();
    alert_pub_.reset();
    gear_state_pub_.reset();
    sim_state_sub_.reset();
    flight_model_sub_.reset();
    flight_controls_sub_.reset();
    failure_injection_sub_.reset();
    caps_sub_.reset();
    latest_fms_.reset();
    latest_controls_.reset();
    latest_caps_.reset();
    model_.reset();
    loader_.reset();
    RCLCPP_INFO(this->get_logger(), "sim_gear cleaned up");
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
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::GearState>::SharedPtr gear_state_pub_;

  // Subscriptions
  rclcpp::Subscription<sim_msgs::msg::SimState>::SharedPtr sim_state_sub_;
  rclcpp::Subscription<sim_msgs::msg::FlightModelState>::SharedPtr flight_model_sub_;
  rclcpp::Subscription<sim_msgs::msg::FlightControls>::SharedPtr flight_controls_sub_;
  rclcpp::Subscription<sim_msgs::msg::FailureInjection>::SharedPtr failure_injection_sub_;
  rclcpp::Subscription<sim_msgs::msg::FlightModelCapabilities>::SharedPtr caps_sub_;

  // Timers
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;
  rclcpp::TimerBase::SharedPtr update_timer_;

  // Plugin
  std::unique_ptr<pluginlib::ClassLoader<sim_interfaces::IGearModel>> loader_;
  std::shared_ptr<sim_interfaces::IGearModel> model_;

  // Cached latest messages
  sim_msgs::msg::FlightModelState::SharedPtr latest_fms_;
  sim_msgs::msg::FlightControls::SharedPtr latest_controls_;
  sim_msgs::msg::FlightModelCapabilities::SharedPtr latest_caps_;

  // State
  uint8_t sim_state_ = sim_msgs::msg::SimState::STATE_INIT;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<GearNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
