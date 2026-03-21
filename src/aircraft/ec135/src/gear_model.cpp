#include <sim_interfaces/i_gear_model.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <string>

namespace aircraft_ec135
{

class GearModel : public sim_interfaces::IGearModel
{
public:
  void configure(const std::string &) override {}
  void update(double) override {}
  void apply_failure(const std::string &, bool) override {}
};

}  // namespace aircraft_ec135

PLUGINLIB_EXPORT_CLASS(aircraft_ec135::GearModel, sim_interfaces::IGearModel)
