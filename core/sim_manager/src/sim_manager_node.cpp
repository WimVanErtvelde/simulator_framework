#include <rclcpp/rclcpp.hpp>

class SimManagerNode : public rclcpp::Node
{
public:
  SimManagerNode()
  : Node("sim_manager", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "sim_manager started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<SimManagerNode>());
  rclcpp::shutdown();
  return 0;
}
