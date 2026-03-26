#include <sim_interfaces/i_gear_model.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <string>
#include <vector>

namespace aircraft_ec135
{

class GearModel : public sim_interfaces::IGearModel
{
public:
  void configure(const std::string &) override {}

  void update(double /*dt_sec*/,
              const std::vector<bool> & /*gear_on_ground*/,
              const std::vector<float> & /*gear_position_norm*/,
              const std::vector<float> & /*gear_steering_deg*/,
              bool /*gear_handle_down*/,
              bool /*on_ground*/) override {}

  sim_interfaces::GearSnapshot get_snapshot() const override
  {
    return sim_interfaces::GearSnapshot();
  }

  void reset() override {}
  void apply_failure(const std::string &, bool) override {}
};

}  // namespace aircraft_ec135

PLUGINLIB_EXPORT_CLASS(aircraft_ec135::GearModel, sim_interfaces::IGearModel)
