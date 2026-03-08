#include <rclcpp/rclcpp.hpp>

class PressurizationNode : public rclcpp::Node
{
public:
  PressurizationNode() : Node("pressurization_node") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PressurizationNode>());
  rclcpp::shutdown();
  return 0;
}
