#ifndef FLIGHT_MODEL_ADAPTER__JSBSIM_ADAPTER_HPP_
#define FLIGHT_MODEL_ADAPTER__JSBSIM_ADAPTER_HPP_

#include "flight_model_adapter/IFlightModelAdapter.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace JSBSim { class FGFDMExec; }

namespace flight_model_adapter
{

class JSBSimAdapter : public IFlightModelAdapter
{
public:
  JSBSimAdapter();
  ~JSBSimAdapter() override;

  bool initialize(const std::string & aircraft_id,
                  const std::string & aircraft_path,
                  const std::string & model_name,
                  const sim_msgs::msg::InitialConditions & default_ic) override;

  void apply_initial_conditions(
    const sim_msgs::msg::InitialConditions & ic) override;

  bool step(double dt_sec) override;

  sim_msgs::msg::FlightModelState get_state() const override;

  FlightModelCapabilities get_capabilities() const override;

  void apply_engine_commands(const sim_msgs::msg::EngineCommands & cmd) override;

  void apply_failure(const std::string & method,
                     const std::string & params_json,
                     bool active) override;

  void write_back_electrical(const sim_msgs::msg::ElectricalState & state) override;
  void write_back_fuel(const sim_msgs::msg::FuelState & state) override;

  void set_property(const std::string & name, double value) override;
  double get_property(const std::string & name) const override;

  void refine_terrain_altitude(double alt_msl_m, double terrain_elev_m) override;

  /// Set max engine RPM for n1_pct computation (piston: propeller-rpm / max → %).
  void set_max_engine_rpm(double rpm) { if (rpm > 0.0) max_engine_rpm_ = rpm; }

  /// Set an optional error logger. If not set, errors go to stderr.
  void set_error_logger(std::function<void(const std::string &)> logger);

private:
  void log_error(const std::string & msg) const;
  std::function<void(const std::string &)> error_logger_;
  std::unique_ptr<JSBSim::FGFDMExec> exec_;
  std::string aircraft_id_;
  double internal_dt_{1.0 / 120.0};   // JSBSim default timestep
  bool initialized_{false};

  // Saved pre-failure values for clean restore on clear
  std::map<int, double> saved_oil_pressure_;  // engine_index → pre-failure PSI

  // Active fuel drain failures: tank_index → rate_lph
  std::map<int, float> active_drains_;

  // Engine RPM scaling for n1_pct computation (from aircraft config.yaml)
  double max_engine_rpm_{2700.0};
};

}  // namespace flight_model_adapter

#endif  // FLIGHT_MODEL_ADAPTER__JSBSIM_ADAPTER_HPP_
