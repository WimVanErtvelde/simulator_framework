#include <rclcpp/rclcpp.hpp>

class SimManagerNode : public rclcpp::Node
{
public:
  SimManagerNode() : Node("sim_manager") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<SimManagerNode>());
  rclcpp::shutdown();
  return 0;
}
