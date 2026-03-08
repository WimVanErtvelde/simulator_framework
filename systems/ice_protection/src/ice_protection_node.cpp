#include <rclcpp/rclcpp.hpp>

class IceProtectionNode : public rclcpp::Node
{
public:
  IceProtectionNode() : Node("ice_protection_node") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<IceProtectionNode>());
  rclcpp::shutdown();
  return 0;
}
