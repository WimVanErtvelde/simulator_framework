#include <rclcpp/rclcpp.hpp>

class EnginesNode : public rclcpp::Node
{
public:
  EnginesNode() : Node("engines_node") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<EnginesNode>());
  rclcpp::shutdown();
  return 0;
}
