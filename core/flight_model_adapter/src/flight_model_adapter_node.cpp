#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/header.hpp>

class FlightModelAdapterNode : public rclcpp::Node
{
public:
  FlightModelAdapterNode()
  : Node("flight_model_adapter", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::Header>(
      "/sim/heartbeat/flight_model_adapter", 10);
    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::Header();
        msg.stamp = this->now();
        msg.frame_id = "flight_model_adapter";
        heartbeat_pub_->publish(msg);
      });
    RCLCPP_INFO(this->get_logger(), "flight_model_adapter started");
  }

private:
  rclcpp::Publisher<std_msgs::msg::Header>::SharedPtr heartbeat_pub_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FlightModelAdapterNode>());
  rclcpp::shutdown();
  return 0;
}
