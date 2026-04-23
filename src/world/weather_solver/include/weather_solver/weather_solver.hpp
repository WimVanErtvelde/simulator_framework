#ifndef WEATHER_SOLVER__WEATHER_SOLVER_HPP_
#define WEATHER_SOLVER__WEATHER_SOLVER_HPP_

#include "weather_solver/dryden_turbulence.hpp"
#include "weather_solver/gust_modulator.hpp"
#include "weather_solver/microburst_model.hpp"
#include <sim_msgs/msg/weather_state.hpp>
#include <sim_msgs/msg/weather_patch.hpp>
#include <sim_msgs/msg/weather_wind_layer.hpp>

#include <vector>
#include <cstdint>

namespace weather_solver
{

/// Standalone weather computation library — no ROS2 dependency.
/// Interpolates wind layers at aircraft altitude, computes ISA + deviations,
/// adds Dryden turbulence perturbations.
class WeatherSolver
{
public:
  struct Config {
    double update_rate_hz = 50.0;
  };

  struct AtmoResult {
    // ISA + deviations
    double temperature_k     = 0.0;
    double pressure_pa       = 0.0;
    double density_kgm3      = 0.0;
    double speed_of_sound_ms = 0.0;
    double oat_k             = 0.0;
    double qnh_pa            = 0.0;
    double density_altitude_m  = 0.0;
    double pressure_altitude_m = 0.0;
    // Wind (sum of ambient + turbulence)
    double wind_north_ms = 0.0;
    double wind_east_ms  = 0.0;
    double wind_down_ms  = 0.0;
    // Metadata
    bool  visible_moisture     = false;
    float turbulence_intensity = 0.0f;
    // Patch-aware runway condition index at aircraft position (0-15).
    // Equals global WeatherState.runway_friction unless inside a patch with
    // override_runway.
    uint8_t runway_condition_idx = 0;
  };

  void configure(const Config & cfg);
  void set_weather(const sim_msgs::msg::WeatherState & weather);
  AtmoResult compute(double dt_sec, double altitude_msl_m, double altitude_agl_m,
                     double tas_ms,
                     double lat_deg = 0.0, double lon_deg = 0.0,
                     double sim_time_sec = 0.0);
  void reset();

private:
  // ISA computation
  static constexpr double ISA_T0        = 288.15;
  static constexpr double ISA_P0        = 101325.0;
  static constexpr double ISA_RHO0      = 1.225;
  static constexpr double ISA_LAPSE     = 0.0065;
  static constexpr double ISA_R         = 287.058;
  static constexpr double ISA_GAMMA     = 1.4;
  static constexpr double ISA_TROPO_ALT = 11000.0;
  static constexpr double ISA_T_TROPO   = 216.65;
  static constexpr double ISA_P_TROPO   = 22632.1;
  static constexpr double ISA_G         = 9.80665;
  static constexpr double HYPSO_SCALE   = 44330.77;
  static constexpr double HYPSO_P_EXP   = 0.190284;
  static constexpr double HYPSO_D_EXP   = 0.234969;

  /// Interpolated wind at a given altitude from wind_layers.
  struct InterpolatedWind {
    double speed_ms      = 0.0;
    double direction_deg = 0.0;
    double vertical_ms   = 0.0;
    double gust_speed_ms = 0.0;  // authored peak at this altitude
    double turbulence    = 0.0;  // 0-1
  };

  /// Wind interpolation takes the source layer vector explicitly so the
  /// same math can sample either the global weather_.wind_layers or a
  /// patch's wind_layers override (Slice 5b-iv-a).
  InterpolatedWind interpolate_wind_from(
      double altitude_m,
      const std::vector<sim_msgs::msg::WeatherWindLayer> & layers) const;

  /// Great-circle distance between two lat/lon points in meters.
  /// Haversine formula — accurate at training scale (< 500 NM error
  /// is well under a meter).
  static double haversine_distance_m(
      double lat1_deg, double lon1_deg,
      double lat2_deg, double lon2_deg);

  /// Smallest-radius patch containing the aircraft, or nullptr if the
  /// aircraft is outside all patches. Pointer is only valid for the
  /// current compute() call — weather_.patches may be replaced by the
  /// next set_weather().
  const sim_msgs::msg::WeatherPatch * find_active_patch(
      double aircraft_lat_deg, double aircraft_lon_deg) const;

  sim_msgs::msg::WeatherState weather_;
  DrydenTurbulence dryden_;
  GustModulator    gust_;
  std::vector<MicroburstModel::HazardParams> mb_params_;
  bool weather_received_ = false;
};

}  // namespace weather_solver

#endif  // WEATHER_SOLVER__WEATHER_SOLVER_HPP_
