#include "weather_solver/gust_modulator.hpp"

#include <chrono>

namespace weather_solver
{

void GustModulator::configure(const Config & cfg)
{
  if (cfg.seed != 0) {
    rng_.seed(cfg.seed);
  } else {
    rng_.seed(static_cast<uint32_t>(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  }
  reset();
}

void GustModulator::reset()
{
  state_ = State::IDLE;
  elapsed_s_ = 0.0;
  idle_duration_s_ = randomize_idle();
}

double GustModulator::randomize_idle()
{
  std::uniform_real_distribution<double> dist(IDLE_MIN_S, IDLE_MAX_S);
  return dist(rng_);
}

double GustModulator::step(double dt_sec)
{
  // Compute factor at the current (unadvanced) state for dt_sec <= 0.
  // Inlined rather than a helper method — trivial switch, keeps the API
  // surface minimal.
  if (dt_sec <= 0.0) {
    switch (state_) {
      case State::IDLE:      return 0.0;
      case State::RAMP_UP:   return elapsed_s_ / RAMP_UP_S;
      case State::PEAK_HOLD: return 1.0;
      case State::RAMP_DOWN: return 1.0 - (elapsed_s_ / RAMP_DOWN_S);
    }
    return 0.0;
  }

  elapsed_s_ += dt_sec;

  // Process state transitions. Loop because a very large dt_sec could
  // cross multiple phases — normally one transition per call.
  while (true) {
    switch (state_) {
      case State::IDLE:
        if (elapsed_s_ >= idle_duration_s_) {
          elapsed_s_ -= idle_duration_s_;
          state_ = State::RAMP_UP;
          continue;
        }
        return 0.0;

      case State::RAMP_UP:
        if (elapsed_s_ >= RAMP_UP_S) {
          elapsed_s_ -= RAMP_UP_S;
          state_ = State::PEAK_HOLD;
          continue;
        }
        return elapsed_s_ / RAMP_UP_S;   // 0 → 1

      case State::PEAK_HOLD:
        if (elapsed_s_ >= HOLD_S) {
          elapsed_s_ -= HOLD_S;
          state_ = State::RAMP_DOWN;
          continue;
        }
        return 1.0;

      case State::RAMP_DOWN:
        if (elapsed_s_ >= RAMP_DOWN_S) {
          elapsed_s_ -= RAMP_DOWN_S;
          state_ = State::IDLE;
          idle_duration_s_ = randomize_idle();
          continue;
        }
        return 1.0 - (elapsed_s_ / RAMP_DOWN_S);   // 1 → 0
    }
  }
}

}  // namespace weather_solver
