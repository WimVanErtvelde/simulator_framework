#include <rclcpp/rclcpp.hpp>

class CigiBridgeNode : public rclcpp::Node
{
public:
  CigiBridgeNode()
  : Node("cigi_bridge", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "cigi_bridge started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CigiBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
