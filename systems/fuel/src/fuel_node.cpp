#include <rclcpp/rclcpp.hpp>

class FuelNode : public rclcpp::Node
{
public:
  FuelNode() : Node("fuel_node") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FuelNode>());
  rclcpp::shutdown();
  return 0;
}
