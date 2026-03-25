#ifndef FLIGHT_MODEL_ADAPTER__IFLIGHT_MODEL_ADAPTER_HPP_
#define FLIGHT_MODEL_ADAPTER__IFLIGHT_MODEL_ADAPTER_HPP_

#include <string>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/initial_conditions.hpp>
#include <sim_msgs/msg/engine_commands.hpp>
#include <sim_msgs/msg/electrical_state.hpp>
#include <sim_msgs/msg/fuel_state.hpp>

namespace flight_model_adapter
{

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
