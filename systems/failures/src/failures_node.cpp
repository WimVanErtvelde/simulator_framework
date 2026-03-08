#include <rclcpp/rclcpp.hpp>

class FailuresNode : public rclcpp::Node
{
public:
  FailuresNode() : Node("failures_node") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FailuresNode>());
  rclcpp::shutdown();
  return 0;
}
