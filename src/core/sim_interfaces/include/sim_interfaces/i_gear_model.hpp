#ifndef SIM_INTERFACES__I_GEAR_MODEL_HPP_
#define SIM_INTERFACES__I_GEAR_MODEL_HPP_

#include <string>
#include <vector>
#include <cstdint>

namespace sim_interfaces
{

struct GearLegState {
  std::string name;
  float position_norm = 1.0f;   // 0=retracted, 1=extended
  bool weight_on_wheels = false;
  uint8_t status = 1;          // 0=UP, 1=DOWN_LOCKED, 2=IN_TRANSIT, 3=FAULT
};

struct GearSnapshot {
  std::string gear_type;
  bool retractable = false;
  std::vector<GearLegState> legs;
  bool on_ground = false;
  bool gear_unsafe = false;
  bool gear_warning = false;
  float nosewheel_angle_deg = 0.0f;
};

class IGearModel
{
public:
  virtual ~IGearModel() = default;
  virtual void configure(const std::string & yaml_path) = 0;

  // Update with flight model data. Called at 50 Hz.
  // gear_on_ground: per-leg ground contact from FlightModelState
  // gear_position_norm: per-leg position from FlightModelState
  // gear_steering_deg: nosewheel steering angle from FlightModelState
  // gear_handle_down: gear lever commanded position
  // on_ground: aggregate on_ground from FlightModelState
  virtual void update(double dt_sec,
                      const std::vector<bool> & gear_on_ground,
                      const std::vector<float> & gear_position_norm,
                      const std::vector<float> & gear_steering_deg,
                      bool gear_handle_down,
                      bool on_ground) = 0;

  virtual GearSnapshot get_snapshot() const = 0;
  virtual void reset() = 0;
  virtual void apply_failure(const std::string & failure_id, bool active) = 0;
};

}  // namespace sim_interfaces

#endif  // SIM_INTERFACES__I_GEAR_MODEL_HPP_
