#include <rclcpp/rclcpp.hpp>

class ElectricalNode : public rclcpp::Node
{
public:
  ElectricalNode() : Node("electrical_node") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ElectricalNode>());
  rclcpp::shutdown();
  return 0;
}
