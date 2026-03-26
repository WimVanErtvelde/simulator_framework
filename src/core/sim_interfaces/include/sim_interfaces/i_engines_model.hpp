#ifndef SIM_INTERFACES__I_ENGINES_MODEL_HPP_
#define SIM_INTERFACES__I_ENGINES_MODEL_HPP_

#include <string>
#include <array>
#include <vector>
#include <sim_msgs/msg/flight_model_state.hpp>

namespace sim_interfaces
{

/// Engine operating state — represents the state machine for each engine.
enum class EngineRunState : uint8_t {
  OFF       = 0,
  CRANKING  = 1,
  STARTING  = 2,
  RUNNING   = 3,
  SHUTDOWN  = 4,
  FAILED    = 5
};

/// Structured control inputs assembled by the engines_node wrapper.
/// Fixed superset — all aircraft use the same struct, zero unused fields.
/// REASON: input_arbitrator needs to validate/route, IOS needs known field names.
struct EngineInputs
{
  // Power management (normalised 0–1)
  std::array<float, 4> throttle_norm        = {};
  std::array<float, 4> power_lever_norm     = {};
  std::array<float, 4> prop_lever_norm      = {};
  std::array<float, 4> condition_lever_norm = {};
  std::array<float, 4> mixture_norm         = {};
  std::array<float, 4> emergency_power_norm = {};

  // Discrete switches
  std::array<bool, 4> starter   = {};
  std::array<bool, 4> ignition  = {};
  std::array<bool, 4> fuel_cutoff = {};

  // Systems coupling (filled by framework from electrical/fuel state)
  float bus_voltage = 0.0f;
  std::array<bool, 4> fuel_available = {};
};

/// Engine state output — float32 for instrument-grade precision.
/// Per-engine arrays indexed [0..engine_count-1], max 4 engines.
/// Zeros for unused fields — consumers check engine_count.
struct EngineStateData
{
  uint8_t engine_count = 0;

  // Per-engine state machine
  std::array<EngineRunState, 4> state = {};

  // Core parameters
  std::array<float, 4> n1_pct       = {};
  std::array<float, 4> n2_pct       = {};
  std::array<float, 4> engine_rpm   = {};

  // Temperatures — technically different measurement points
  std::array<float, 4> itt_degc     = {};  // Inter-Turbine Temperature
  std::array<float, 4> egt_degc     = {};  // Exhaust Gas Temperature
  std::array<float, 4> tot_degc     = {};  // Turbine Outlet Temperature
  std::array<float, 4> cht_degc     = {};  // Cylinder Head Temperature (piston)

  // Pressures & flow
  std::array<float, 4> oil_press_kpa         = {};
  std::array<float, 4> oil_temp_degc         = {};
  std::array<float, 4> fuel_flow_kgph        = {};
  std::array<float, 4> manifold_press_inhg   = {};

  // Power output
  std::array<float, 4> torque_nm    = {};
  std::array<float, 4> torque_pct   = {};
  std::array<float, 4> shp_kw      = {};

  // Status flags
  std::array<bool, 4> starter_engaged    = {};
  std::array<bool, 4> generator_online   = {};

  // Drivetrain — scalar, not per-engine (driven by ALL engines through gearbox)
  float prop_rpm       = 0.0f;
  float main_rotor_rpm = 0.0f;
  float tail_rotor_rpm = 0.0f;

  // Warning flags (threshold-driven from aircraft YAML)
  std::array<bool, 4> low_oil_pressure_warning = {};
  std::array<bool, 4> high_egt_warning         = {};
  std::array<bool, 4> high_cht_warning         = {};

  // FADEC mode per engine (NORM/TOGA/FLEX/IDLE/REV/OFF)
  std::array<std::string, 4> fadec_mode = {};
};

/// Switch/selector ID config — populated by plugin from aircraft YAML, read by node wrapper.
struct EngineSwitchConfig
{
  std::vector<std::string> starter_ids;         // per engine index
  std::vector<std::string> ignition_ids;        // magneto/ignition switch per engine
  std::vector<std::string> fuel_cutoff_ids;     // per engine
  std::vector<std::string> prop_lever_ids;      // selector, per engine
  std::vector<std::string> condition_lever_ids; // selector, per engine
  std::vector<std::string> power_lever_ids;     // selector, per engine
};

class IEnginesModel
{
public:
  virtual ~IEnginesModel() = default;

  /// Load configuration from aircraft engine.yaml
  virtual void configure(const std::string & yaml_path) = 0;

  /// Step the engine systems model.
  /// @param dt_sec           time step (0.0 in FROZEN with dirty panel)
  /// @param inputs           structured control inputs (assembled by node wrapper)
  /// @param fdm              latest FlightModelState (for FDM passthrough readings)
  /// @param active_failures  list of currently active failure IDs
  virtual void update(
      double dt_sec,
      const EngineInputs & inputs,
      const sim_msgs::msg::FlightModelState & fdm,
      const std::vector<std::string> & active_failures) = 0;

  /// Force engine to running or off state (autostart / instant positioning).
  virtual void set_running(uint8_t engine_index, bool running) = 0;

  /// Number of engines for this aircraft.
  virtual uint8_t get_engine_count() const = 0;

  /// Reset to initial conditions (from already-parsed config, not re-read YAML).
  virtual void reset() = 0;

  /// Get current state snapshot for ROS2 publishing.
  virtual EngineStateData get_state() const = 0;

  /// Get panel switch/selector ID configuration (read from aircraft YAML).
  virtual EngineSwitchConfig get_switch_config() const = 0;
};

}  // namespace sim_interfaces

#endif  // SIM_INTERFACES__I_ENGINES_MODEL_HPP_
