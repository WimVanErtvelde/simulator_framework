#include <rclcpp/rclcpp.hpp>

class HydraulicNode : public rclcpp::Node
{
public:
  HydraulicNode()
  : Node("hydraulic_node", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "hydraulic_node started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<HydraulicNode>());
  rclcpp::shutdown();
  return 0;
}
