#ifndef WEATHER_SOLVER__MICROBURST_MODEL_HPP_
#define WEATHER_SOLVER__MICROBURST_MODEL_HPP_

#include <cstdint>
#include <vector>

namespace weather_solver
{

/// Oseguera-Bowles 1988 microburst wind field model.
/// Standalone library — no ROS2 dependency.
class MicroburstModel
{
public:
  struct HazardParams {
    double north_m = 0.0;           // offset from reference point
    double east_m  = 0.0;
    float  core_radius_m     = 0.0f;
    float  shaft_altitude_m  = 0.0f;
    float  intensity         = 0.0f; // lambda (m/s)
    uint8_t lifecycle_phase  = 0;    // 0=Dormant, 1=Intensifying, 2=Mature, 3=Decaying
    float  activation_time_sec = 0.0f;
  };

  struct WindContribution {
    double north_ms = 0.0;
    double east_ms  = 0.0;
    double down_ms  = 0.0;
  };

  /// Sample all active microbursts at the given aircraft position.
  static WindContribution sample(
      const std::vector<HazardParams> & hazards,
      double aircraft_north_m,
      double aircraft_east_m,
      double altitude_agl_m,
      double sim_time_sec);

  /// Convert lat/lon to north/east offset from a reference point.
  /// Flat-earth approximation, adequate for distances < 50 NM.
  static void latlon_to_ne(double ref_lat_deg, double ref_lon_deg,
                           double pt_lat_deg, double pt_lon_deg,
                           double & north_m, double & east_m);
};

}  // namespace weather_solver

#endif  // WEATHER_SOLVER__MICROBURST_MODEL_HPP_
