#ifndef FLIGHT_MODEL_ADAPTER__JSBSIM__SURFACE_WRITEBACK_HPP_
#define FLIGHT_MODEL_ADAPTER__JSBSIM__SURFACE_WRITEBACK_HPP_

#include <cstdint>

#include "flight_model_adapter/IFlightModelAdapter.hpp"

namespace JSBSim { class FGFDMExec; }

namespace jsbsim_writeback
{

/// Write runway surface friction into the JSBSim property tree by combining
/// terrain-type and runway-contamination multipliers from the configured
/// lookup tables. Writes ground/static-friction-factor and
/// ground/rolling_friction-factor, which FGGroundReactions reads globally
/// each tick and every landing-gear leg picks up.
///
/// surface_type     — framework enum from HatHotResponse (0..10)
/// runway_friction  — WeatherState enum 0..15
/// on_ground        — when false, resets factors to 1.0 (baseline) so the
///                     next landing starts from nominal JSBSim ground physics
/// tables           — loaded from aircraft config.yaml
void write_surface(JSBSim::FGFDMExec * exec,
                   uint8_t surface_type,
                   uint8_t runway_friction,
                   bool on_ground,
                   const flight_model_adapter::GroundFrictionTables & tables);

}  // namespace jsbsim_writeback

#endif  // FLIGHT_MODEL_ADAPTER__JSBSIM__SURFACE_WRITEBACK_HPP_
