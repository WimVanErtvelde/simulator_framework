#include "flight_model_adapter/jsbsim/JSBSimElectricalWriteback.hpp"

#include <FGFDMExec.h>
#include <input_output/FGPropertyManager.h>

#include <algorithm>
#include <string>

namespace jsbsim_writeback
{

/// Safely set a property value, creating the node if it doesn't exist.
/// FGFDMExec::SetPropertyValue can throw std::string when the node is missing;
/// going through GetNode(path, true) guarantees creation first.
static void safe_set(JSBSim::FGFDMExec * exec,
                     const std::string & path, double value)
{
  auto * node = exec->GetPropertyManager()->GetNode(path, true);
  if (node) {
    node->setDoubleValue(value);
  }
}

void write_electrical(JSBSim::FGFDMExec * exec,
                      const sim_msgs::msg::ElectricalState & state)
{
  if (!exec) return;

  // Master bus voltage — used by JSBSim starter motor / relay scripts
  safe_set(exec, "systems/electrical/bus-volts",
           static_cast<double>(state.master_bus_voltage));

  // Per-bus voltages — write both indexed form (for JSBSim system scripts)
  // and named form (for aircraft-specific scripts)
  const std::size_t n = std::min(state.bus_names.size(), state.bus_voltages.size());
  for (std::size_t i = 0; i < n; ++i) {
    const double v = static_cast<double>(state.bus_voltages[i]);
    safe_set(exec, "systems/electrical/bus[" + std::to_string(i) + "]/voltage", v);
    if (!state.bus_names[i].empty()) {
      safe_set(exec, "systems/electrical/" + state.bus_names[i] + "/voltage", v);
    }
  }

  // Battery state of charge
  safe_set(exec, "systems/electrical/battery/charge-pct",
           static_cast<double>(state.battery_soc_pct));
}

}  // namespace jsbsim_writeback
