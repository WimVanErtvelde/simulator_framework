#include <sim_interfaces/i_electrical_model.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <iostream>
#include <string>

namespace aircraft_ec135
{

/// Stub — EC135 electrical model awaiting v2 graph YAML migration.
class ElectricalModel : public sim_interfaces::IElectricalModel
{
public:
  void configure(const std::string & /*yaml_path*/) override
  {
    std::cerr << "[aircraft_ec135::ElectricalModel] WARNING: stub — "
                 "no electrical simulation active. Awaiting v2 YAML migration.\n";
  }

  void update(double /*dt_sec*/) override {}
  void apply_failure(const std::string & /*failure_id*/, bool /*active*/) override {}
  void command_switch(const std::string & /*id*/, int /*cmd*/) override {}
  void set_engine_n2(const std::vector<double> & /*n2_pct*/) override {}
  void set_ground_state(bool /*on_ground*/, bool /*ext_pwr*/) override {}
  void reset() override {}

  sim_interfaces::ElectricalSnapshot get_snapshot() const override
  {
    return sim_interfaces::ElectricalSnapshot{};
  }
};

}  // namespace aircraft_ec135

PLUGINLIB_EXPORT_CLASS(aircraft_ec135::ElectricalModel, sim_interfaces::IElectricalModel)
