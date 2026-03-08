#include <rclcpp/rclcpp.hpp>

class NavigationNode : public rclcpp::Node
{
public:
  NavigationNode() : Node("navigation_node") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<NavigationNode>());
  rclcpp::shutdown();
  return 0;
}
