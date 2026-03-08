#include <rclcpp/rclcpp.hpp>

class InputArbitratorNode : public rclcpp::Node
{
public:
  InputArbitratorNode()
  : Node("input_arbitrator", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "input_arbitrator started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<InputArbitratorNode>());
  rclcpp::shutdown();
  return 0;
}
