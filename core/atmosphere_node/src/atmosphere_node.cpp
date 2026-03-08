#include <rclcpp/rclcpp.hpp>

class AtmosphereNode : public rclcpp::Node
{
public:
  AtmosphereNode() : Node("atmosphere_node") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<AtmosphereNode>());
  rclcpp::shutdown();
  return 0;
}
