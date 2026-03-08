#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/header.hpp>

class AtmosphereNode : public rclcpp::Node
{
public:
  AtmosphereNode()
  : Node("atmosphere_node", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::Header>(
      "/sim/heartbeat/atmosphere_node", 10);
    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::Header();
        msg.stamp = this->now();
        msg.frame_id = "atmosphere_node";
        heartbeat_pub_->publish(msg);
      });
    RCLCPP_INFO(this->get_logger(), "atmosphere_node started");
  }

private:
  rclcpp::Publisher<std_msgs::msg::Header>::SharedPtr heartbeat_pub_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<AtmosphereNode>());
  rclcpp::shutdown();
  return 0;
}
