#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <std_msgs/msg/string.hpp>
#include <pluginlib/class_loader.hpp>
#include <sim_interfaces/i_gear_model.hpp>
#include <sim_msgs/msg/sim_alert.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include <fstream>

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
        this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE);
        this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE);
      });
    RCLCPP_INFO(this->get_logger(), "sim_gear constructed (unconfigured)");
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle_state", 10);
    alert_pub_ = this->create_publisher<sim_msgs::msg::SimAlert>(
      "/sim/alerts", 10);

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
    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::String();
        msg.data = this->get_name();
        heartbeat_pub_->publish(msg);
      });

    constexpr double dt_sec = 0.02;
    update_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(20),
      [this, dt_sec]() { model_->update(dt_sec); });

    RCLCPP_INFO(this->get_logger(), "sim_gear activated");
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    heartbeat_timer_.reset();
    update_timer_.reset();
    RCLCPP_INFO(this->get_logger(), "sim_gear deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    heartbeat_pub_.reset();
    lifecycle_state_pub_.reset();
    alert_pub_.reset();
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

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr heartbeat_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr lifecycle_state_pub_;
  rclcpp::Publisher<sim_msgs::msg::SimAlert>::SharedPtr alert_pub_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;
  rclcpp::TimerBase::SharedPtr update_timer_;
  std::unique_ptr<pluginlib::ClassLoader<sim_interfaces::IGearModel>> loader_;
  std::shared_ptr<sim_interfaces::IGearModel> model_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<GearNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
