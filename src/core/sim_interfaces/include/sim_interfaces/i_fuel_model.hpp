#ifndef SIM_INTERFACES__I_FUEL_MODEL_HPP_
#define SIM_INTERFACES__I_FUEL_MODEL_HPP_

#include <string>
#include <vector>
#include <sim_msgs/msg/fuel_state.hpp>
#include <sim_msgs/msg/panel_controls.hpp>

namespace sim_interfaces
{

class IFuelModel
{
public:
  virtual ~IFuelModel() = default;
  virtual void configure(const std::string & yaml_path) = 0;
  virtual void update(
      double dt_sec,
      const std::vector<float> & engine_fuel_flow_kgs,
      const sim_msgs::msg::PanelControls & panel,
      const std::vector<std::string> & active_failures) = 0;
  virtual void apply_initial_conditions(float fuel_total_norm) = 0;
  virtual void reset() = 0;
  virtual sim_msgs::msg::FuelState get_state() const = 0;
};

}  // namespace sim_interfaces

#endif  // SIM_INTERFACES__I_FUEL_MODEL_HPP_
