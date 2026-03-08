#include <rclcpp/rclcpp.hpp>

class CigiBridgeNode : public rclcpp::Node
{
public:
  CigiBridgeNode() : Node("cigi_bridge") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CigiBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
