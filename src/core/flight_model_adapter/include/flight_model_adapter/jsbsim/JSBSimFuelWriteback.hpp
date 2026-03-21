#ifndef FLIGHT_MODEL_ADAPTER__JSBSIM__FUEL_WRITEBACK_HPP_
#define FLIGHT_MODEL_ADAPTER__JSBSIM__FUEL_WRITEBACK_HPP_

#include <sim_msgs/msg/fuel_state.hpp>

namespace JSBSim { class FGFDMExec; }

namespace jsbsim_writeback
{

/// Write fuel tank quantities from sim_fuel into JSBSim property tree.
void write_fuel(JSBSim::FGFDMExec * exec,
                const sim_msgs::msg::FuelState & state);

}  // namespace jsbsim_writeback

#endif  // FLIGHT_MODEL_ADAPTER__JSBSIM__FUEL_WRITEBACK_HPP_
