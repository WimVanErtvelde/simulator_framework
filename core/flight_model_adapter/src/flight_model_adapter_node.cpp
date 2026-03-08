#include <rclcpp/rclcpp.hpp>

class FlightModelAdapterNode : public rclcpp::Node
{
public:
  FlightModelAdapterNode() : Node("flight_model_adapter") {}
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FlightModelAdapterNode>());
  rclcpp::shutdown();
  return 0;
}
