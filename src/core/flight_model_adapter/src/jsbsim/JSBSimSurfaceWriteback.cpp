#include "flight_model_adapter/jsbsim/JSBSimSurfaceWriteback.hpp"

#include <FGFDMExec.h>
#include <input_output/FGPropertyManager.h>

#include <string>

namespace jsbsim_writeback
{

static void safe_set(JSBSim::FGFDMExec * exec,
                     const std::string & path, double value)
{
  auto * node = exec->GetPropertyManager()->GetNode(path, true);
  if (node) {
    node->setDoubleValue(value);
  }
}

void write_surface(JSBSim::FGFDMExec * exec,
                   uint8_t surface_type,
                   uint8_t runway_condition_idx,
                   bool on_ground,
                   const flight_model_adapter::GroundFrictionTables & tables)
{
  if (!exec) return;

  double static_ff  = 1.0;
  double rolling_ff = 1.0;

  if (on_ground) {
    const auto surf = tables.lookup_surface(surface_type);
    const auto cont = tables.lookup_contamination(runway_condition_idx);
    static_ff  = surf.static_ff  * cont.static_ff;
    rolling_ff = surf.rolling_ff * cont.rolling_ff;
  }

  // JSBSim FGSurface property names — note the inconsistent dash/underscore
  // convention is deliberate (matches JSBSim source).
  safe_set(exec, "ground/rolling_friction-factor", rolling_ff);
  safe_set(exec, "ground/static-friction-factor",  static_ff);
}

}  // namespace jsbsim_writeback
