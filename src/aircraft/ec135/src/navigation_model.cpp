#include <sim_interfaces/i_navigation_model.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <string>

namespace aircraft_ec135
{

class NavigationModel : public sim_interfaces::INavigationModel
{
public:
  void configure(const std::string &) override {}
  void update(double) override {}
  void apply_failure(const std::string &, bool) override {}
};

}  // namespace aircraft_ec135

PLUGINLIB_EXPORT_CLASS(aircraft_ec135::NavigationModel, sim_interfaces::INavigationModel)
