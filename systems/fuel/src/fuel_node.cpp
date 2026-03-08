#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/header.hpp>

class FuelNode : public rclcpp::Node
{
public:
  FuelNode()
  : Node("fuel_node", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::Header>(
      "/sim/heartbeat/sim_fuel", 10);
    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::Header();
        msg.stamp = this->now();
        msg.frame_id = "sim_fuel";
        heartbeat_pub_->publish(msg);
      });
    RCLCPP_INFO(this->get_logger(), "fuel_node started");
  }

private:
  rclcpp::Publisher<std_msgs::msg::Header>::SharedPtr heartbeat_pub_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FuelNode>());
  rclcpp::shutdown();
  return 0;
}
