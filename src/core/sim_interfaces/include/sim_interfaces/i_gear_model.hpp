#ifndef SIM_INTERFACES__I_GEAR_MODEL_HPP_
#define SIM_INTERFACES__I_GEAR_MODEL_HPP_

#include <string>

namespace sim_interfaces
{

class IGearModel
{
public:
  virtual ~IGearModel() = default;
  virtual void configure(const std::string & yaml_path) = 0;
  virtual void update(double dt_sec) = 0;
  virtual void apply_failure(const std::string & failure_id, bool active) = 0;
};

}  // namespace sim_interfaces

#endif  // SIM_INTERFACES__I_GEAR_MODEL_HPP_
