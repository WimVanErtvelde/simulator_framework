#include "flight_model_adapter/jsbsim/JSBSimFuelWriteback.hpp"

#include <FGFDMExec.h>
#include <input_output/FGPropertyManager.h>

#include <algorithm>
#include <string>

namespace jsbsim_writeback
{

static constexpr double KG_TO_LBS = 1.0 / 0.453592;

/// Safely set a property value, creating the node if it doesn't exist.
static void safe_set(JSBSim::FGFDMExec * exec,
                     const std::string & path, double value)
{
  auto * node = exec->GetPropertyManager()->GetNode(path, true);
  if (node) {
    node->setDoubleValue(value);
  }
}

void write_fuel(JSBSim::FGFDMExec * exec,
                const sim_msgs::msg::FuelState & state)
{
  if (!exec) return;

  int count = static_cast<int>(state.tank_count);

  for (int i = 0; i < count; ++i) {
    double contents_lbs = static_cast<double>(state.tank_quantity_kg[i]) * KG_TO_LBS;
    std::string path = "propulsion/tank[" + std::to_string(i) + "]/contents-lbs";
    safe_set(exec, path, contents_lbs);
  }
}

}  // namespace jsbsim_writeback
