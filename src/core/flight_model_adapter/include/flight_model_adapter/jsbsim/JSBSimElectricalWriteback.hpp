#ifndef FLIGHT_MODEL_ADAPTER__JSBSIM__ELECTRICAL_WRITEBACK_HPP_
#define FLIGHT_MODEL_ADAPTER__JSBSIM__ELECTRICAL_WRITEBACK_HPP_

#include <sim_msgs/msg/electrical_state.hpp>

namespace JSBSim { class FGFDMExec; }

namespace jsbsim_writeback
{

/// Write bus voltages from sim_electrical into JSBSim property tree.
void write_electrical(JSBSim::FGFDMExec * exec,
                      const sim_msgs::msg::ElectricalState & state);

}  // namespace jsbsim_writeback

#endif  // FLIGHT_MODEL_ADAPTER__JSBSIM__ELECTRICAL_WRITEBACK_HPP_
