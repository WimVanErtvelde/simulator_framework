#include <rclcpp/rclcpp.hpp>

class FailuresNode : public rclcpp::Node
{
public:
  FailuresNode()
  : Node("failures_node", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "failures_node started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FailuresNode>());
  rclcpp::shutdown();
  return 0;
}
