#include <rclcpp/rclcpp.hpp>

class EnginesNode : public rclcpp::Node
{
public:
  EnginesNode()
  : Node("engines_node", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "engines_node started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<EnginesNode>());
  rclcpp::shutdown();
  return 0;
}
