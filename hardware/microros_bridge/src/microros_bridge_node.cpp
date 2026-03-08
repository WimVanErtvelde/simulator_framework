#include <rclcpp/rclcpp.hpp>

class MicroRosBridgeNode : public rclcpp::Node
{
public:
  MicroRosBridgeNode() : Node("microros_bridge") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<MicroRosBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
