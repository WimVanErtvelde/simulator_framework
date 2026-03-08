#include <rclcpp/rclcpp.hpp>

class ElectricalNode : public rclcpp::Node
{
public:
  ElectricalNode()
  : Node("electrical_node", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "electrical_node started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ElectricalNode>());
  rclcpp::shutdown();
  return 0;
}
