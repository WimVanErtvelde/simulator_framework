#ifndef FLIGHT_MODEL_ADAPTER__IFLIGHT_MODEL_ADAPTER_HPP_
#define FLIGHT_MODEL_ADAPTER__IFLIGHT_MODEL_ADAPTER_HPP_

#include <array>
#include <string>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/initial_conditions.hpp>
#include <sim_msgs/msg/engine_commands.hpp>
#include <sim_msgs/msg/electrical_state.hpp>
#include <sim_msgs/msg/fuel_state.hpp>
#include <sim_msgs/msg/atmosphere_state.hpp>
#include <sim_msgs/msg/payload_command.hpp>

namespace flight_model_adapter
{

/// Per-surface friction multipliers. One entry per framework surface enum
/// (HatHotResponse.surface_type: UNKNOWN=0..MARSH=10) and per contamination
/// level (WeatherState.runway_condition_idx 0=Dry..15=Snow+Ice max).
/// The effective factor applied to JSBSim's ground solver is the product
/// of the surface and contamination entries.
struct GroundFrictionTables {
  struct Factors {
    double static_ff  = 1.0;   // braking / static grip multiplier
    double rolling_ff = 1.0;   // rolling-drag multiplier
  };

  static constexpr std::size_t kSurfaceCount       = 11;  // 0..10
  static constexpr std::size_t kContaminationCount = 16;  // 0..15

  std::array<Factors, kSurfaceCount>       surface{};        // indexed by surface_type
  std::array<Factors, kContaminationCount> contamination{};  // indexed by runway_condition_idx

  /// Clamped lookup: if the enum index is out of range, index 0 (UNKNOWN/Dry) is used.
  Factors lookup_surface(uint8_t idx) const {
    return surface[idx < kSurfaceCount ? idx : 0];
  }
  Factors lookup_contamination(uint8_t idx) const {
    return contamination[idx < kContaminationCount ? idx : 0];
  }
};

/// Three-way capability mode per subsystem.
/// Determines whether the FDM or external systems nodes own a subsystem,
/// and whether write-back from the external node to the FDM is required.
enum class CapabilityMode {
  FDM_NATIVE,           // FDM models it natively — our node defers, reads FDM output
  EXTERNAL_COUPLED,     // We model it, FDM needs our output — writeback required each cycle
  EXTERNAL_DECOUPLED    // We model it, FDM doesn't use it — no writeback needed
};

/// Describes which subsystems the flight model handles internally.
/// Consumers check these modes to decide whether to run their own solver
/// or defer to the flight model's native implementation.
struct FlightModelCapabilities
{
  // Electrical / hydraulic
  CapabilityMode electrical        = CapabilityMode::FDM_NATIVE;
  CapabilityMode hydraulic         = CapabilityMode::FDM_NATIVE;

  // Gear and ground
  CapabilityMode gear_retract      = CapabilityMode::FDM_NATIVE;

  // Fuel system
  CapabilityMode fuel_quantities    = CapabilityMode::FDM_NATIVE;
  CapabilityMode fuel_pump_pressure = CapabilityMode::FDM_NATIVE;
  CapabilityMode fuel_crossfeed     = CapabilityMode::FDM_NATIVE;

  // Propeller / FADEC
  CapabilityMode prop_governor     = CapabilityMode::FDM_NATIVE;
  CapabilityMode beta_range        = CapabilityMode::FDM_NATIVE;
  CapabilityMode reverse_thrust    = CapabilityMode::FDM_NATIVE;
  CapabilityMode fadec             = CapabilityMode::FDM_NATIVE;
  CapabilityMode autothrust        = CapabilityMode::FDM_NATIVE;

  // Common engine
  CapabilityMode starter           = CapabilityMode::FDM_NATIVE;
  CapabilityMode engine_fire       = CapabilityMode::FDM_NATIVE;

  // Pneumatic / bleed / APU
  CapabilityMode pneumatic         = CapabilityMode::FDM_NATIVE;
  CapabilityMode bleed_air         = CapabilityMode::FDM_NATIVE;
  CapabilityMode apu               = CapabilityMode::FDM_NATIVE;

  // Turbine-specific
  CapabilityMode fuel_control_unit = CapabilityMode::FDM_NATIVE;
  CapabilityMode ignition_sequence = CapabilityMode::FDM_NATIVE;
};

class IFlightModelAdapter
{
public:
  virtual ~IFlightModelAdapter() = default;

  /// Load the flight model aircraft model.
  /// @param aircraft_id   Aircraft type key (e.g. "c172")
  /// @param aircraft_path Root path containing flight model aircraft data directories
  /// @param model_name    FDM-specific model name (e.g. "c172p" for JSBSim). Empty = use aircraft_id.
  /// @param default_ic    Default initial conditions from aircraft config.yaml
  /// @return true on success
  virtual bool initialize(const std::string & aircraft_id,
                           const std::string & aircraft_path,
                           const std::string & model_name,
                           const sim_msgs::msg::InitialConditions & default_ic) = 0;

  /// Apply initial conditions (position, config, etc.)
  virtual void apply_initial_conditions(
    const sim_msgs::msg::InitialConditions & ic) = 0;

  /// Advance the flight model by dt_sec seconds. May sub-step internally.
  /// @return true if the step completed successfully
  virtual bool step(double dt_sec) = 0;

  /// Extract the current flight model state into a ROS message.
  virtual sim_msgs::msg::FlightModelState get_state() const = 0;

  /// Report which subsystems the flight model handles natively.
  virtual FlightModelCapabilities get_capabilities() const = 0;

  /// Apply engine commands from sim_engine_systems (write-back).
  /// Default no-op — only overridden by adapters that need turboprop/FADEC write-back.
  virtual void apply_engine_commands(const sim_msgs::msg::EngineCommands & /*cmd*/) {}

  /// Called by sim_failures when a flight_model handler failure is injected or cleared.
  /// method: abstract failure method name (e.g. "set_engine_running")
  /// params_json: flat JSON string of parameters (e.g. {"engine_index":0,"value":false})
  /// active: true = inject, false = clear/reverse
  virtual void apply_failure(const std::string & /*method*/,
                             const std::string & /*params_json*/,
                             bool /*active*/) {}

  /// Write-back electrical state from sim_electrical to the FDM.
  /// Called each cycle when electrical capability is EXTERNAL_COUPLED.
  virtual void write_back_electrical(const sim_msgs::msg::ElectricalState & /*state*/) {}

  /// Write-back fuel state from sim_fuel to the FDM.
  /// Called each cycle when fuel_quantities capability is EXTERNAL_COUPLED.
  virtual void write_back_fuel(const sim_msgs::msg::FuelState & /*state*/) {}

  /// Write-back atmosphere state (wind, temperature, pressure) to the FDM.
  /// Called each step before Run() so the FDM sees authored weather.
  virtual void write_back_atmosphere(const sim_msgs::msg::AtmosphereState & /*state*/,
                                     double /*altitude_msl_m*/) {}

  /// Install the ground-friction lookup tables (loaded from aircraft
  /// config.yaml). Called once after initialize(). If never called, the
  /// adapter falls back to baseline factors (1.0 × 1.0) for every input.
  virtual void set_ground_friction_tables(const GroundFrictionTables & /*tables*/) {}

  /// Write-back surface friction to the FDM ground solver. Combines terrain
  /// surface type (from HAT responses, framework enum 0..10) and runway
  /// contamination (WeatherState 0..15) into a single effective factor.
  /// Called each step before Run().
  virtual void write_back_surface(uint8_t /*surface_type*/,
                                   uint8_t /*runway_condition_idx*/,
                                   bool /*on_ground*/) {}

  /// Apply payload station weight command from IOS.
  virtual void apply_payload_command(const sim_msgs::msg::PayloadCommand & /*cmd*/) {}

  /// Apply fuel tank load command from IOS (direct tank write, no RunIC).
  virtual void apply_fuel_load_command(const sim_msgs::msg::PayloadCommand & /*cmd*/) {}

  /// Set a named FDM property (e.g. "fcs/aileron-cmd-norm").
  /// Used by the node to write arbitrated controls before each step.
  virtual void set_property(const std::string & /*name*/, double /*value*/) {}

  /// Read a named FDM property. Returns 0.0 if not found.
  virtual double get_property(const std::string & /*name*/) const { return 0.0; }

  /// Refine altitude and terrain after HOT response — does NOT touch lat/lon/heading.
  virtual void refine_terrain_altitude(double /*alt_msl_m*/, double /*terrain_elev_m*/) {}
};

}  // namespace flight_model_adapter

#endif  // FLIGHT_MODEL_ADAPTER__IFLIGHT_MODEL_ADAPTER_HPP_
