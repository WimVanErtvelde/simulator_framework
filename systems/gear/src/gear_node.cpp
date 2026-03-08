#include <rclcpp/rclcpp.hpp>

class GearNode : public rclcpp::Node
{
public:
  GearNode()
  : Node("gear_node", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "gear_node started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<GearNode>());
  rclcpp::shutdown();
  return 0;
}
