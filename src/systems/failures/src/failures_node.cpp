#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <lifecycle_msgs/msg/state.hpp>
#include <std_msgs/msg/string.hpp>
#include <sim_msgs/msg/failure_state.hpp>
#include <sim_msgs/msg/failure_command.hpp>
#include <sim_msgs/msg/failure_injection.hpp>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/sim_alert.hpp>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <yaml-cpp/yaml.h>

#include <map>
#include <set>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

// ─── Internal data structures ───────────────────────────────────────────────

struct FailureDef {
  std::string id;
  std::string display_name;
  std::string category;
  std::string handler;
  std::string method;
  std::map<std::string, std::string> params;  // stored as string key-value pairs
};

struct ArmedEntry {
  std::string failure_id;
  std::string trigger_mode;        // "delay" or "condition"
  float delay_remaining_s;
  std::string condition_param;
  std::string condition_operator;
  float condition_value;
  float condition_duration_s;
  float condition_met_duration_s;  // accumulator
};

// ─── Helper: build params_json from map (no external JSON library) ──────────

static std::string build_params_json(const std::map<std::string, std::string> & params)
{
  std::ostringstream oss;
  oss << "{";
  bool first = true;
  for (const auto & [k, v] : params) {
    if (!first) oss << ",";
    first = false;
    oss << "\"" << k << "\":";
    // Try to detect numeric values — pass through unquoted
    bool is_number = false;
    if (!v.empty()) {
      char * end = nullptr;
      std::strtod(v.c_str(), &end);
      if (end && *end == '\0') is_number = true;
    }
    // Booleans
    if (v == "true" || v == "false") {
      oss << v;
    } else if (is_number) {
      oss << v;
    } else {
      oss << "\"" << v << "\"";
    }
  }
  oss << "}";
  return oss.str();
}

// ─── Unit conversions ────────────────────────────────────────────────────────
static constexpr double MS_TO_KT  = 1.94384;   // m/s → knots
static constexpr double M_TO_FT   = 3.28084;   // metres → feet
static constexpr double MS_TO_FPM = 196.85;    // m/s → feet/minute

// ─── Extract FDM condition param from FlightModelState ──────────────────────

static double extract_fdm_param(
  const sim_msgs::msg::FlightModelState & fdm,
  const std::string & param)
{
  if (param == "airspeed_kt") {
    return fdm.ias_ms * MS_TO_KT;
  } else if (param == "altitude_ft_msl") {
    return fdm.altitude_msl_m * M_TO_FT;
  } else if (param == "altitude_ft_agl") {
    return fdm.altitude_agl_m * M_TO_FT;
  } else if (param == "vertical_speed_fpm") {
    return fdm.vertical_speed_ms * MS_TO_FPM;
  } else if (param == "on_ground") {
    return fdm.on_ground ? 1.0 : 0.0;
  } else if (param == "fuel_total_kg") {
    return static_cast<double>(fdm.fuel_total_kg);
  }
  return 0.0;
}

// ─── Evaluate condition ─────────────────────────────────────────────────────

static bool evaluate_condition(
  double actual, const std::string & op, double target)
{
  if (op == "less_than") return actual < target;
  if (op == "greater_than") return actual > target;
  if (op == "equals") return std::fabs(actual - target) < 1e-6;
  if (op == "less_than_or_equal") return actual <= target;
  if (op == "greater_than_or_equal") return actual >= target;
  return false;
}

// ═════════════════════════════════════════════════════════════════════════════

class FailuresNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  FailuresNode()
  : LifecycleNode("sim_failures", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    this->declare_parameter<std::string>("aircraft_id", "c172");

    auto_start_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      [this]() {
        auto_start_timer_->cancel();
        auto_start_timer_.reset();
        auto st = this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE);
        if (st.id() != lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE) {
          RCLCPP_ERROR(this->get_logger(), "Auto-start: configure failed — stays unconfigured");
          return;
        }
        this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE);
      });
    RCLCPP_INFO(this->get_logger(), "sim_failures constructed (unconfigured)");
  }

  // ─── on_configure ───────────────────────────────────────────────────────

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    // Diagnostics publishers (rclcpp::Publisher — publish in ALL states)
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle_state", 10);
    alert_pub_ = this->create_publisher<sim_msgs::msg::SimAlert>(
      "/sim/alerts", 10);

    // Load failures.yaml catalog
    std::string aircraft_id = this->get_parameter("aircraft_id").as_string();
    std::string yaml_path =
      ament_index_cpp::get_package_share_directory("aircraft_" + aircraft_id) +
      "/config/failures.yaml";

    try {
      YAML::Node root = YAML::LoadFile(yaml_path);
      if (!root["failures"]) {
        RCLCPP_ERROR(this->get_logger(),
          "No 'failures' key in %s", yaml_path.c_str());
        publish_alert("Missing 'failures' key in " + yaml_path);
        return CallbackReturn::FAILURE;
      }

      catalog_.clear();
      for (const auto & entry : root["failures"]) {
        FailureDef def;
        def.id = entry["id"].as<std::string>();
        def.display_name = entry["display_name"].as<std::string>();
        def.category = entry["category"].as<std::string>();
        def.handler = entry["injection"]["handler"].as<std::string>();
        def.method = entry["injection"]["method"].as<std::string>();

        if (entry["injection"]["params"]) {
          for (const auto & p : entry["injection"]["params"]) {
            def.params[p.first.as<std::string>()] = p.second.as<std::string>();
          }
        }

        catalog_[def.id] = def;
      }

      RCLCPP_INFO(this->get_logger(),
        "Loaded %zu failure definitions from %s", catalog_.size(), yaml_path.c_str());
    } catch (const std::exception & e) {
      RCLCPP_ERROR(this->get_logger(),
        "Failed to load failures.yaml: %s", e.what());
      publish_alert("Failed to load " + yaml_path + ": " + e.what());
      return CallbackReturn::FAILURE;
    }

    // Clear runtime state
    active_failures_.clear();
    armed_queue_.clear();
    failed_nav_receivers_.clear();
    failed_instruments_.clear();

    publish_lifecycle_state("inactive");
    RCLCPP_INFO(this->get_logger(), "sim_failures configured");
    return CallbackReturn::SUCCESS;
  }

  // ─── on_activate ────────────────────────────────────────────────────────

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    // Lifecycle publishers for FailureState and FailureInjection routing
    failure_state_pub_ = this->create_publisher<sim_msgs::msg::FailureState>(
      "/sim/failure_state", 10);
    fdm_cmd_pub_ = this->create_publisher<sim_msgs::msg::FailureInjection>(
      "/sim/failure/flight_model_commands", 10);
    elec_cmd_pub_ = this->create_publisher<sim_msgs::msg::FailureInjection>(
      "/sim/failure/electrical_commands", 10);
    navaid_cmd_pub_ = this->create_publisher<sim_msgs::msg::FailureInjection>(
      "/sim/failure/navaid_commands", 10);
    air_data_cmd_pub_ = this->create_publisher<sim_msgs::msg::FailureInjection>(
      "/sim/failure/air_data_commands", 10);

    // Subscriptions
    cmd_sub_ = this->create_subscription<sim_msgs::msg::FailureCommand>(
      "/devices/instructor/failure_command", 10,
      std::bind(&FailuresNode::on_failure_command, this, std::placeholders::_1));

    fdm_sub_ = this->create_subscription<sim_msgs::msg::FlightModelState>(
      "/sim/flight_model/state", 10,
      [this](sim_msgs::msg::FlightModelState::SharedPtr msg) {
        latest_fdm_state_ = *msg;
        have_fdm_state_ = true;
      });

    // 10 Hz timer — evaluate armed queue and publish FailureState
    eval_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      std::bind(&FailuresNode::on_eval_timer, this));

    // 1 Hz heartbeat
    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::String();
        msg.data = this->get_name();
        heartbeat_pub_->publish(msg);
      });

    publish_lifecycle_state("active");
    RCLCPP_INFO(this->get_logger(), "sim_failures activated");
    return CallbackReturn::SUCCESS;
  }

  // ─── on_deactivate ──────────────────────────────────────────────────────

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    eval_timer_.reset();
    heartbeat_timer_.reset();
    cmd_sub_.reset();
    fdm_sub_.reset();
    failure_state_pub_.reset();
    fdm_cmd_pub_.reset();
    elec_cmd_pub_.reset();
    navaid_cmd_pub_.reset();
    air_data_cmd_pub_.reset();
    RCLCPP_INFO(this->get_logger(), "sim_failures deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  // ─── on_cleanup ─────────────────────────────────────────────────────────

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    heartbeat_pub_.reset();
    lifecycle_state_pub_.reset();
    alert_pub_.reset();
    catalog_.clear();
    active_failures_.clear();
    armed_queue_.clear();
    failed_nav_receivers_.clear();
    failed_instruments_.clear();
    active_params_override_.clear();
    RCLCPP_INFO(this->get_logger(), "sim_failures cleaned up");
    publish_lifecycle_state("unconfigured");
    return CallbackReturn::SUCCESS;
  }

private:
  // ─── FailureCommand callback ────────────────────────────────────────────

  void on_failure_command(const sim_msgs::msg::FailureCommand::SharedPtr msg)
  {
    if (msg->action == "inject") {
      inject_failure(msg->failure_id, msg->params_override_json);
    } else if (msg->action == "arm") {
      arm_failure(msg);
    } else if (msg->action == "clear") {
      clear_failure(msg->failure_id);
    } else if (msg->action == "clear_all") {
      clear_all();
    } else {
      RCLCPP_WARN(this->get_logger(),
        "Unknown failure command action: '%s'", msg->action.c_str());
    }
  }

  // ─── Inject a failure immediately ───────────────────────────────────────

  void inject_failure(const std::string & failure_id,
                      const std::string & params_override_json = "")
  {
    auto it = catalog_.find(failure_id);
    if (it == catalog_.end()) {
      RCLCPP_WARN(this->get_logger(),
        "Unknown failure_id: '%s'", failure_id.c_str());
      return;
    }

    const auto & def = it->second;

    // Handle sim_failures-internal failures
    if (def.handler == "sim_failures") {
      if (def.method == "set_nav_receiver_failed") {
        auto pit = def.params.find("receiver_id");
        if (pit != def.params.end()) {
          failed_nav_receivers_.insert(pit->second);
        }
      } else if (def.method == "set_instrument_failed") {
        auto pit = def.params.find("instrument_id");
        if (pit != def.params.end()) {
          failed_instruments_.insert(pit->second);
        }
      }
    } else {
      // Route to external handler topic
      auto inj = sim_msgs::msg::FailureInjection();
      inj.failure_id = failure_id;
      inj.handler = def.handler;
      inj.method = def.method;
      // Use params_override_json if provided, otherwise build from catalog
      if (!params_override_json.empty()) {
        inj.params_json = params_override_json;
      } else {
        inj.params_json = build_params_json(def.params);
      }
      inj.active = true;
      route_injection(inj);
    }

    // Store the override so clear can use it too
    if (!params_override_json.empty()) {
      active_params_override_[failure_id] = params_override_json;
    }

    active_failures_.insert(failure_id);
    RCLCPP_INFO(this->get_logger(), "Injected failure: %s", failure_id.c_str());
  }

  // ─── Arm a failure with trigger ─────────────────────────────────────────

  void arm_failure(const sim_msgs::msg::FailureCommand::SharedPtr & cmd)
  {
    if (catalog_.find(cmd->failure_id) == catalog_.end()) {
      RCLCPP_WARN(this->get_logger(),
        "Cannot arm unknown failure_id: '%s'", cmd->failure_id.c_str());
      return;
    }

    ArmedEntry entry;
    entry.failure_id = cmd->failure_id;
    entry.trigger_mode = cmd->trigger_mode;
    entry.delay_remaining_s = cmd->trigger_delay_s;
    entry.condition_param = cmd->condition_param;
    entry.condition_operator = cmd->condition_operator;
    entry.condition_value = cmd->condition_value;
    entry.condition_duration_s = cmd->condition_duration_s;
    entry.condition_met_duration_s = 0.0f;

    armed_queue_.push_back(entry);
    RCLCPP_INFO(this->get_logger(),
      "Armed failure: %s (mode=%s)", cmd->failure_id.c_str(), cmd->trigger_mode.c_str());
  }

  // ─── Clear a single failure ─────────────────────────────────────────────

  void clear_failure(const std::string & failure_id)
  {
    auto it = catalog_.find(failure_id);
    if (it != catalog_.end()) {
      const auto & def = it->second;

      if (def.handler == "sim_failures") {
        if (def.method == "set_nav_receiver_failed") {
          auto pit = def.params.find("receiver_id");
          if (pit != def.params.end()) {
            failed_nav_receivers_.erase(pit->second);
          }
        } else if (def.method == "set_instrument_failed") {
          auto pit = def.params.find("instrument_id");
          if (pit != def.params.end()) {
            failed_instruments_.erase(pit->second);
          }
        }
      } else {
        auto inj = sim_msgs::msg::FailureInjection();
        inj.failure_id = failure_id;
        inj.handler = def.handler;
        inj.method = def.method;
        // Use stored params override if available (e.g. world_navaid station_id)
        auto ov = active_params_override_.find(failure_id);
        if (ov != active_params_override_.end()) {
          inj.params_json = ov->second;
        } else {
          inj.params_json = build_params_json(def.params);
        }
        inj.active = false;
        route_injection(inj);
      }
    }

    active_params_override_.erase(failure_id);
    active_failures_.erase(failure_id);

    // Remove from armed queue
    armed_queue_.erase(
      std::remove_if(armed_queue_.begin(), armed_queue_.end(),
        [&](const ArmedEntry & e) { return e.failure_id == failure_id; }),
      armed_queue_.end());

    RCLCPP_INFO(this->get_logger(), "Cleared failure: %s", failure_id.c_str());
  }

  // ─── Clear all failures ─────────────────────────────────────────────────

  void clear_all()
  {
    // Publish active=false for every active failure
    for (const auto & fid : active_failures_) {
      auto it = catalog_.find(fid);
      if (it == catalog_.end()) continue;
      const auto & def = it->second;
      if (def.handler != "sim_failures") {
        auto inj = sim_msgs::msg::FailureInjection();
        inj.failure_id = fid;
        inj.handler = def.handler;
        inj.method = def.method;
        auto ov = active_params_override_.find(fid);
        if (ov != active_params_override_.end()) {
          inj.params_json = ov->second;
        } else {
          inj.params_json = build_params_json(def.params);
        }
        inj.active = false;
        route_injection(inj);
      }
    }

    active_failures_.clear();
    armed_queue_.clear();
    failed_nav_receivers_.clear();
    failed_instruments_.clear();
    active_params_override_.clear();
    RCLCPP_INFO(this->get_logger(), "Cleared all failures");
  }

  // ─── Route injection to handler-specific topic ──────────────────────────

  void route_injection(const sim_msgs::msg::FailureInjection & inj)
  {
    if (inj.handler == "flight_model") {
      fdm_cmd_pub_->publish(inj);
    } else if (inj.handler == "electrical") {
      elec_cmd_pub_->publish(inj);
    } else if (inj.handler == "navaid_sim") {
      navaid_cmd_pub_->publish(inj);
    } else if (inj.handler == "air_data") {
      air_data_cmd_pub_->publish(inj);
    } else {
      RCLCPP_WARN(this->get_logger(),
        "Unknown handler '%s' for failure '%s'",
        inj.handler.c_str(), inj.failure_id.c_str());
    }
  }

  // ─── 10 Hz timer: evaluate armed queue + publish state ──────────────────

  void on_eval_timer()
  {
    constexpr float dt = 0.1f;

    // Evaluate armed entries
    std::vector<std::string> to_inject;
    for (auto & entry : armed_queue_) {
      if (entry.trigger_mode == "delay") {
        entry.delay_remaining_s -= dt;
        if (entry.delay_remaining_s <= 0.0f) {
          to_inject.push_back(entry.failure_id);
        }
      } else if (entry.trigger_mode == "condition" && have_fdm_state_) {
        double actual = extract_fdm_param(latest_fdm_state_, entry.condition_param);
        bool met = evaluate_condition(actual, entry.condition_operator, entry.condition_value);
        if (met) {
          entry.condition_met_duration_s += dt;
          if (entry.condition_met_duration_s >= entry.condition_duration_s) {
            to_inject.push_back(entry.failure_id);
          }
        } else {
          entry.condition_met_duration_s = 0.0f;
        }
      }
    }

    // Inject triggered entries and remove from armed queue
    for (const auto & fid : to_inject) {
      inject_failure(fid);
      armed_queue_.erase(
        std::remove_if(armed_queue_.begin(), armed_queue_.end(),
          [&](const ArmedEntry & e) { return e.failure_id == fid; }),
        armed_queue_.end());
    }

    // Publish FailureState
    auto state_msg = sim_msgs::msg::FailureState();
    state_msg.stamp = this->now();

    state_msg.active_failure_ids.assign(
      active_failures_.begin(), active_failures_.end());

    for (const auto & entry : armed_queue_) {
      state_msg.armed_failure_ids.push_back(entry.failure_id);
      if (entry.trigger_mode == "delay") {
        state_msg.armed_trigger_remaining_s.push_back(entry.delay_remaining_s);
      } else {
        state_msg.armed_trigger_remaining_s.push_back(-1.0f);
      }
    }

    state_msg.failed_nav_receivers.assign(
      failed_nav_receivers_.begin(), failed_nav_receivers_.end());
    state_msg.failed_instruments.assign(
      failed_instruments_.begin(), failed_instruments_.end());

    failure_state_pub_->publish(state_msg);
  }

  // ─── Helpers ────────────────────────────────────────────────────────────

  void publish_lifecycle_state(const std::string & state)
  {
    if (lifecycle_state_pub_) {
      auto msg = std_msgs::msg::String();
      msg.data = std::string(this->get_name()) + ":" + state;
      lifecycle_state_pub_->publish(msg);
    }
  }

  void publish_alert(const std::string & message)
  {
    if (alert_pub_) {
      auto alert = sim_msgs::msg::SimAlert();
      alert.header.stamp = this->now();
      alert.severity = sim_msgs::msg::SimAlert::SEVERITY_CRITICAL;
      alert.source = this->get_name();
      alert.message = message;
      alert_pub_->publish(alert);
    }
  }

  // ─── Members ────────────────────────────────────────────────────────────

  // Catalog loaded from failures.yaml
  std::map<std::string, FailureDef> catalog_;

  // Runtime state
  std::set<std::string> active_failures_;
  std::vector<ArmedEntry> armed_queue_;
  std::set<std::string> failed_nav_receivers_;
  std::set<std::string> failed_instruments_;
  std::map<std::string, std::string> active_params_override_;  // failure_id → params_json override
  sim_msgs::msg::FlightModelState latest_fdm_state_;
  bool have_fdm_state_ = false;

  // Publishers (rclcpp::Publisher — publish in all states)
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr heartbeat_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr lifecycle_state_pub_;
  rclcpp::Publisher<sim_msgs::msg::SimAlert>::SharedPtr alert_pub_;

  // Lifecycle publishers
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::FailureState>::SharedPtr failure_state_pub_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::FailureInjection>::SharedPtr fdm_cmd_pub_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::FailureInjection>::SharedPtr elec_cmd_pub_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::FailureInjection>::SharedPtr navaid_cmd_pub_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::FailureInjection>::SharedPtr air_data_cmd_pub_;

  // Subscriptions
  rclcpp::Subscription<sim_msgs::msg::FailureCommand>::SharedPtr cmd_sub_;
  rclcpp::Subscription<sim_msgs::msg::FlightModelState>::SharedPtr fdm_sub_;

  // Timers
  rclcpp::TimerBase::SharedPtr eval_timer_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;
};

// ═════════════════════════════════════════════════════════════════════════════

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<FailuresNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
