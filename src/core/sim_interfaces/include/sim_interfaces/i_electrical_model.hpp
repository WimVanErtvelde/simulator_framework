#ifndef SIM_INTERFACES__I_ELECTRICAL_MODEL_HPP_
#define SIM_INTERFACES__I_ELECTRICAL_MODEL_HPP_

#include <string>
#include <vector>

namespace sim_interfaces
{

/// Snapshot of electrical system state for publishing to ROS2.
struct ElectricalSnapshot
{
  struct BusInfo   { std::string name; float voltage; bool powered; };
  struct SourceInfo{ std::string name; bool active; float voltage; float current; float battery_soc; };
  struct LoadInfo  { std::string name; bool powered; float current; };
  struct SwitchInfo{ std::string id; std::string label; bool closed; };
  struct CbInfo    { std::string name; bool closed; bool tripped; };

  std::vector<BusInfo>    buses;
  std::vector<SourceInfo> sources;
  std::vector<LoadInfo>   loads;
  std::vector<SwitchInfo> switches;
  std::vector<CbInfo>     cbs;

  float total_load_amps = 0.0f;
  float battery_soc_pct = 0.0f;
  float master_bus_voltage = 0.0f;
  bool  avionics_bus_powered = false;
  bool  essential_bus_powered = false;
};

class IElectricalModel
{
public:
  virtual ~IElectricalModel() = default;
  virtual void configure(const std::string & yaml_path) = 0;
  virtual void update(double dt_sec) = 0;
  virtual void apply_failure(const std::string & failure_id, bool active) = 0;

  /// Command a switch (0=open, 1=close, 2=toggle)
  virtual void command_switch(const std::string & id, int cmd) = 0;

  /// Feed engine N2 percentages for alternator/generator drive
  virtual void set_engine_n2(const std::vector<double> & n2_pct) = 0;

  /// Feed ground/external-power state from FDM and IOS
  virtual void set_ground_state(bool on_ground, bool external_power_connected) = 0;

  /// Reset to initial conditions (battery SOC, switch defaults from YAML)
  virtual void reset() = 0;

  /// Get current state snapshot for ROS2 publishing
  virtual ElectricalSnapshot get_snapshot() const = 0;
};

}  // namespace sim_interfaces

#endif  // SIM_INTERFACES__I_ELECTRICAL_MODEL_HPP_
