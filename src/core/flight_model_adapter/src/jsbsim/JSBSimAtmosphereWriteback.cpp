#include "flight_model_adapter/jsbsim/JSBSimAtmosphereWriteback.hpp"

#include <FGFDMExec.h>
#include <input_output/FGPropertyManager.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace jsbsim_writeback
{

static constexpr double M_TO_FT  = 3.28084;
static constexpr double PA_TO_PSF = 0.0208854;
static constexpr double ISA_T0    = 288.15;     // sea-level ISA temperature (K)
static constexpr double ISA_LAPSE = 0.0065;     // troposphere lapse rate (K/m)
static constexpr double K_TO_R    = 9.0 / 5.0;  // Kelvin delta → Rankine delta

/// Safely set a property value, creating the node if it doesn't exist.
static void safe_set(JSBSim::FGFDMExec * exec,
                     const std::string & path, double value)
{
  auto * node = exec->GetPropertyManager()->GetNode(path, true);
  if (node) {
    node->setDoubleValue(value);
  }
}

void write_atmosphere(JSBSim::FGFDMExec * exec,
                      const sim_msgs::msg::AtmosphereState & state,
                      double altitude_msl_m)
{
  if (!exec) return;

  // Wind — NED components in ft/s
  safe_set(exec, "atmosphere/wind-north-fps", state.wind_north_ms * M_TO_FT);
  safe_set(exec, "atmosphere/wind-east-fps",  state.wind_east_ms  * M_TO_FT);
  safe_set(exec, "atmosphere/wind-down-fps",  state.wind_down_ms  * M_TO_FT);

  // Temperature deviation from ISA at current altitude (in Rankine)
  double alt_clamped = std::max(0.0, std::min(altitude_msl_m, 11000.0));
  double t_isa_k = ISA_T0 - ISA_LAPSE * alt_clamped;
  double delta_t_r = (state.oat_k - t_isa_k) * K_TO_R;
  safe_set(exec, "atmosphere/delta-T", delta_t_r);

  // Pressure — only write if we have a valid value (avoid zeroing on startup)
  if (state.pressure_pa > 1000.0) {
    safe_set(exec, "atmosphere/P-psf", state.pressure_pa * PA_TO_PSF);
  }
}

}  // namespace jsbsim_writeback
