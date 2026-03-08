#include <rclcpp/rclcpp.hpp>

class FuelNode : public rclcpp::Node
{
public:
  FuelNode()
  : Node("fuel_node", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "fuel_node started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FuelNode>());
  rclcpp::shutdown();
  return 0;
}
