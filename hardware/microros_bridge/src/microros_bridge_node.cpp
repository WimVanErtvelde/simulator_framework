#include <rclcpp/rclcpp.hpp>

class MicroRosBridgeNode : public rclcpp::Node
{
public:
  MicroRosBridgeNode()
  : Node("microros_bridge", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "microros_bridge started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<MicroRosBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
