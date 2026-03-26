#include <sim_interfaces/i_gear_model.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <yaml-cpp/yaml.h>
#include <string>
#include <stdexcept>

namespace aircraft_c172
{

class GearModel : public sim_interfaces::IGearModel
{
public:
  void configure(const std::string & yaml_path) override
  {
    YAML::Node root = YAML::LoadFile(yaml_path);
    auto gear = root["gear"];
    if (!gear) {
      throw std::runtime_error("gear.yaml: missing 'gear' key");
    }

    gear_type_  = gear["type"] ? gear["type"].as<std::string>() : "fixed_tricycle";
    retractable_ = gear["retractable"] ? gear["retractable"].as<bool>() : false;

    legs_.clear();
    auto legs_node = gear["legs"];
    if (!legs_node || !legs_node.IsSequence()) {
      throw std::runtime_error("gear.yaml: 'gear.legs' must be a sequence");
    }
    for (const auto & leg_node : legs_node) {
      sim_interfaces::GearLegState leg;
      leg.name           = leg_node["name"] ? leg_node["name"].as<std::string>() : "unknown";
      leg.position_norm   = 1.0f;   // fixed gear: always extended
      leg.weight_on_wheels = true; // start on ground
      leg.status         = 1;      // DOWN_LOCKED
      legs_.push_back(leg);
    }

    on_ground_          = true;
    nosewheel_angle_deg_ = 0.0f;
    gear_unsafe_        = false;
    gear_warning_       = false;
    failure_unsafe_active_ = false;
  }

  void update(double /*dt_sec*/,
              const std::vector<bool> & gear_on_ground,
              const std::vector<float> & /*gear_position_norm*/,
              const std::vector<float> & gear_steering_deg,
              bool /*gear_handle_down*/,
              bool on_ground) override
  {
    // Fixed gear: position is always 1.0 (fully extended), status always DOWN_LOCKED.
    // We only track WoW per leg and nosewheel angle.
    for (size_t i = 0; i < legs_.size(); ++i) {
      legs_[i].position_norm = 1.0f;
      legs_[i].status       = 1;  // DOWN_LOCKED
      if (i < gear_on_ground.size()) {
        legs_[i].weight_on_wheels = gear_on_ground[i];
      }
    }

    // Nosewheel steering angle comes from gear index 0
    if (!gear_steering_deg.empty()) {
      nosewheel_angle_deg_ = gear_steering_deg[0];
    }

    on_ground_ = on_ground;

    // Fixed gear has no unsafe/warning condition unless a failure forces it
    gear_unsafe_  = failure_unsafe_active_;
    gear_warning_ = false;  // fixed gear produces no retraction warnings
  }

  sim_interfaces::GearSnapshot get_snapshot() const override
  {
    sim_interfaces::GearSnapshot snap;
    snap.gear_type           = gear_type_;
    snap.retractable         = retractable_;
    snap.legs                = legs_;
    snap.on_ground           = on_ground_;
    snap.gear_unsafe         = gear_unsafe_;
    snap.gear_warning        = gear_warning_;
    snap.nosewheel_angle_deg = nosewheel_angle_deg_;
    return snap;
  }

  void reset() override
  {
    // Restore all legs to down/locked and on ground
    for (auto & leg : legs_) {
      leg.position_norm     = 1.0f;
      leg.weight_on_wheels = true;
      leg.status           = 1;  // DOWN_LOCKED
    }
    on_ground_            = true;
    nosewheel_angle_deg_  = 0.0f;
    gear_unsafe_          = false;
    gear_warning_         = false;
    failure_unsafe_active_ = false;
  }

  void apply_failure(const std::string & failure_id, bool active) override
  {
    // "gear_unsafe_indication" — used for warning light test / IOS failure injection
    if (failure_id == "gear_unsafe_indication") {
      failure_unsafe_active_ = active;
      gear_unsafe_  = active;
      return;
    }
    // Unknown failures are silently ignored — another system node may handle them
  }

private:
  std::string gear_type_    = "fixed_tricycle";
  bool retractable_         = false;
  std::vector<sim_interfaces::GearLegState> legs_;
  bool on_ground_           = true;
  float nosewheel_angle_deg_ = 0.0f;
  bool gear_unsafe_         = false;
  bool gear_warning_        = false;
  bool failure_unsafe_active_ = false;
};

}  // namespace aircraft_c172

PLUGINLIB_EXPORT_CLASS(aircraft_c172::GearModel, sim_interfaces::IGearModel)
