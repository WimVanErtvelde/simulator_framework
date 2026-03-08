#include <rclcpp/rclcpp.hpp>

class PressurizationNode : public rclcpp::Node
{
public:
  PressurizationNode()
  : Node("pressurization_node", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "pressurization_node started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PressurizationNode>());
  rclcpp::shutdown();
  return 0;
}
