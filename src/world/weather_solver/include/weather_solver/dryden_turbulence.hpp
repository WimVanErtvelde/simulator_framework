#ifndef WEATHER_SOLVER__DRYDEN_TURBULENCE_HPP_
#define WEATHER_SOLVER__DRYDEN_TURBULENCE_HPP_

#include <cstdint>
#include <random>

namespace weather_solver
{

/// MIL-F-8785C Dryden continuous turbulence model.
/// Three independent axes (u, v, w) filtered from white noise.
/// Standalone library — no ROS2 dependency.
class DrydenTurbulence
{
public:
  enum class Model {
    NONE          = 0,
    MIL_F_8785C   = 1,
    MIL_HDBK_1797A = 2,  // same as 8785C for now
    ESDU_85020    = 3,    // stub
  };

  struct Config {
    Model model = Model::MIL_F_8785C;
    uint32_t seed = 0;  // 0 = random seed
  };

  struct Perturbation {
    double u_ms = 0.0;  // longitudinal (along wind)
    double v_ms = 0.0;  // lateral (perpendicular, horizontal)
    double w_ms = 0.0;  // vertical
  };

  void configure(const Config & cfg);
  void reset();

  /// Call each step. Returns body-axis perturbation velocities.
  /// severity: 0-1 from interpolated turbulence_severity
  /// altitude_agl_m: aircraft altitude above ground (affects scale lengths)
  /// tas_ms: true airspeed (affects filter bandwidth)
  Perturbation update(double dt_sec, double severity,
                      double altitude_agl_m, double tas_ms);

private:
  std::mt19937 rng_;
  Model model_ = Model::NONE;

  // Filter states
  double state_u_  = 0.0;
  double state_v1_ = 0.0;
  double state_v2_ = 0.0;
  double state_w1_ = 0.0;
  double state_w2_ = 0.0;
};

}  // namespace weather_solver

#endif  // WEATHER_SOLVER__DRYDEN_TURBULENCE_HPP_
