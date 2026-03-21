#ifndef SIM_INTERFACES__I_AIR_DATA_MODEL_HPP_
#define SIM_INTERFACES__I_AIR_DATA_MODEL_HPP_

#include <string>
#include <vector>
#include <cstdint>

namespace sim_interfaces
{

struct AirDataInputs {
  // Truth from FlightModelState
  double tas_ms = 0.0;
  double altitude_msl_m = 0.0;
  double vertical_speed_ms = 0.0;
  double alpha_rad = 0.0;        // angle of attack (for position error correction)

  // From AtmosphereState
  double static_pressure_pa = 101325.0;
  double temperature_k = 288.15;
  double density_kgm3 = 1.225;
  double qnh_pa = 101325.0;

  // From WeatherState
  double turbulence_intensity = 0.0;  // 0.0=calm, 1.0=severe
  bool visible_moisture = false;       // in cloud or precip

  // From ElectricalState (per system, indexed by system)
  std::vector<bool> pitot_heat_powered;  // resolved from electrical load state

  // From PanelControls
  std::vector<bool> alternate_static_selected;  // per system
};

struct AirDataSystemState {
  std::string name;
  float indicated_airspeed_ms = 0.0f;
  float calibrated_airspeed_ms = 0.0f;
  float mach = 0.0f;
  float altitude_indicated_m = 0.0f;
  float altitude_pressure_m = 0.0f;
  float vertical_speed_ms = 0.0f;
  float sat_k = 288.15f;
  float tat_k = 288.15f;
  bool pitot_healthy = true;
  bool static_healthy = true;
  bool pitot_heat_on = false;
  bool alternate_static_active = false;
  float pitot_ice_pct = 0.0f;
};

struct AirDataSnapshot {
  std::vector<AirDataSystemState> systems;
};

class IAirDataModel
{
public:
  virtual ~IAirDataModel() = default;
  virtual void configure(const std::string & yaml_path) = 0;
  virtual void update(double dt_sec, const AirDataInputs & inputs) = 0;
  virtual AirDataSnapshot get_snapshot() const = 0;
  virtual void reset() = 0;
  virtual void apply_failure(const std::string & failure_id, bool active) = 0;

  // Called by the node after configure() to learn which load/switch names to look up
  virtual std::vector<std::string> get_heat_load_names() const = 0;
  virtual std::vector<std::string> get_alternate_static_switch_ids() const = 0;
};

}  // namespace sim_interfaces

#endif  // SIM_INTERFACES__I_AIR_DATA_MODEL_HPP_
