#ifndef FLIGHT_MODEL_ADAPTER__JSBSIM__ATMOSPHERE_WRITEBACK_HPP_
#define FLIGHT_MODEL_ADAPTER__JSBSIM__ATMOSPHERE_WRITEBACK_HPP_

#include <sim_msgs/msg/atmosphere_state.hpp>

namespace JSBSim { class FGFDMExec; }

namespace jsbsim_writeback
{

/// Write wind, temperature deviation, and pressure from AtmosphereState
/// into JSBSim property tree. Called each step before JSBSim Run().
void write_atmosphere(JSBSim::FGFDMExec * exec,
                      const sim_msgs::msg::AtmosphereState & state,
                      double altitude_msl_m);

}  // namespace jsbsim_writeback

#endif  // FLIGHT_MODEL_ADAPTER__JSBSIM__ATMOSPHERE_WRITEBACK_HPP_
