#include <rclcpp/rclcpp.hpp>

class IceProtectionNode : public rclcpp::Node
{
public:
  IceProtectionNode()
  : Node("ice_protection_node", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "ice_protection_node started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<IceProtectionNode>());
  rclcpp::shutdown();
  return 0;
}
