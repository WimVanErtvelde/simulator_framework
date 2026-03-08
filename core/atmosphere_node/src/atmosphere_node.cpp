#include <rclcpp/rclcpp.hpp>

class AtmosphereNode : public rclcpp::Node
{
public:
  AtmosphereNode()
  : Node("atmosphere_node", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    RCLCPP_INFO(this->get_logger(), "atmosphere_node started");
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<AtmosphereNode>());
  rclcpp::shutdown();
  return 0;
}
