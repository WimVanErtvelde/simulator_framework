#include <rclcpp/rclcpp.hpp>

class HydraulicNode : public rclcpp::Node
{
public:
  HydraulicNode() : Node("hydraulic_node") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<HydraulicNode>());
  rclcpp::shutdown();
  return 0;
}
