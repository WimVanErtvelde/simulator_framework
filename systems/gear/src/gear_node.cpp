#include <rclcpp/rclcpp.hpp>

class GearNode : public rclcpp::Node
{
public:
  GearNode() : Node("gear_node") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<GearNode>());
  rclcpp::shutdown();
  return 0;
}
