#ifndef WEATHER_SOLVER__GUST_MODULATOR_HPP_
#define WEATHER_SOLVER__GUST_MODULATOR_HPP_

#include <cstdint>
#include <random>

namespace weather_solver
{

/// Stochastic gust modulator — emits a 0-1 factor representing current
/// gust strength. Output is scaled by the authored (gust_peak - sustained)
/// delta in WeatherSolver; the modulator itself just produces timing.
///
/// State machine:
///   IDLE → RAMP_UP → PEAK_HOLD → RAMP_DOWN → IDLE (random wait) → repeat
///
/// Timing (fixed for MVP):
///   RAMP_UP:   1.5 s (factor 0 → 1)
///   PEAK_HOLD: 1.0 s (factor = 1)
///   RAMP_DOWN: 1.5 s (factor 1 → 0)
///   IDLE:      random 15..45 s (uniform)
///
/// First IDLE period is also randomized so gusts don't start immediately
/// on activation. Seed 0 = wall-clock random; non-zero seed = deterministic.
class GustModulator
{
public:
  enum class State { IDLE, RAMP_UP, PEAK_HOLD, RAMP_DOWN };

  struct Config {
    uint32_t seed = 0;   // 0 = wall-clock random
  };

  void configure(const Config & cfg);
  void reset();

  /// Advance by dt_sec (if <= 0, returns factor at current state without
  /// advancing). Returns current gust factor in [0, 1].
  double step(double dt_sec);

  /// Exposed for diagnostics / tests.
  State state() const { return state_; }

private:
  std::mt19937 rng_;
  State  state_            = State::IDLE;
  double elapsed_s_        = 0.0;
  double idle_duration_s_  = 0.0;

  static constexpr double RAMP_UP_S   = 1.5;
  static constexpr double HOLD_S      = 1.0;
  static constexpr double RAMP_DOWN_S = 1.5;
  static constexpr double IDLE_MIN_S  = 15.0;
  static constexpr double IDLE_MAX_S  = 45.0;

  double randomize_idle();
};

}  // namespace weather_solver

#endif  // WEATHER_SOLVER__GUST_MODULATOR_HPP_
