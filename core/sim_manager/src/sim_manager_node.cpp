#include <rclcpp/rclcpp.hpp>
#include <rosgraph_msgs/msg/clock.hpp>
#include <std_msgs/msg/header.hpp>
#include <sim_msgs/msg/sim_state.hpp>
#include <sim_msgs/msg/sim_command.hpp>
#include <sim_msgs/msg/sim_alert.hpp>
#include <sim_msgs/msg/initial_conditions.hpp>
#include <sim_msgs/msg/scenario_event.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include <yaml-cpp/yaml.h>
#include <sim_manager/nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <algorithm>

using json = nlohmann::json;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Internal structs
// ---------------------------------------------------------------------------

struct InitialConditions {
  double latitude_rad      = 0.0;
  double longitude_rad     = 0.0;
  double altitude_msl_m    = 0.0;
  double heading_rad       = 0.0;
  double airspeed_ms       = 0.0;
  double oat_deviation_k   = 0.0;
  double qnh_pa            = 101325.0;
  double fuel_total_pct    = 1.0;
  std::string configuration = "cold_and_dark";
};

struct ScenarioEvent {
  double at_time_s;
  std::string event_id;
  std::string action;
  std::string params_json;
  bool consumed = false;
};

// ---------------------------------------------------------------------------
// SimManagerNode
// ---------------------------------------------------------------------------

class SimManagerNode : public rclcpp::Node
{
public:
  SimManagerNode()
  : Node("sim_manager", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", false}}))  // sim_manager drives the clock, uses wall time
  {
    // ── Parameters ──────────────────────────────────────────────────────
    this->declare_parameter<std::string>("aircraft_id", "");
    this->declare_parameter<double>("time_scale", 1.0);
    this->declare_parameter<std::string>("aircraft_config_dir", "");

    aircraft_id_ = this->get_parameter("aircraft_id").as_string();
    if (aircraft_id_.empty()) {
      RCLCPP_FATAL(this->get_logger(),
        "aircraft_id parameter is required but not set. Shutting down.");
      throw std::runtime_error("aircraft_id parameter is required");
    }
    time_scale_ = this->get_parameter("time_scale").as_double();

    // ── Load aircraft config ────────────────────────────────────────────
    load_aircraft_config();

    // ── Publishers ──────────────────────────────────────────────────────
    clock_pub_ = this->create_publisher<rosgraph_msgs::msg::Clock>(
      "/clock", rclcpp::QoS(10).best_effort());
    state_pub_ = this->create_publisher<sim_msgs::msg::SimState>(
      "/sim/state", 10);
    alert_pub_ = this->create_publisher<sim_msgs::msg::SimAlert>(
      "/sim/alerts", 10);
    ic_pub_ = this->create_publisher<sim_msgs::msg::InitialConditions>(
      "/sim/initial_conditions", rclcpp::QoS(10).transient_local());
    scenario_event_pub_ = this->create_publisher<sim_msgs::msg::ScenarioEvent>(
      "/sim/scenario/event", 10);
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::Header>(
      "/sim/heartbeat/sim_manager", 10);

    // ── Subscribers ─────────────────────────────────────────────────────
    command_sub_ = this->create_subscription<sim_msgs::msg::SimCommand>(
      "/sim/command", 10,
      std::bind(&SimManagerNode::on_command, this, std::placeholders::_1));

    // ── Heartbeat subscriptions for required nodes ──────────────────────
    for (const auto & name : required_nodes_) {
      last_heartbeat_[name] = std::chrono::steady_clock::time_point::min();
      auto sub = this->create_subscription<std_msgs::msg::Header>(
        "/sim/heartbeat/" + name, 10,
        [this, name](const std_msgs::msg::Header::SharedPtr) {
          last_heartbeat_[name] = std::chrono::steady_clock::now();
        });
      heartbeat_subs_.push_back(sub);
    }

    // ── Timers ──────────────────────────────────────────────────────────
    // Clock at 50 Hz (wall timer — sim_manager is the clock source)
    clock_timer_ = this->create_wall_timer(20ms,
      std::bind(&SimManagerNode::on_clock_tick, this));

    // State broadcast at 10 Hz
    state_timer_ = this->create_wall_timer(100ms,
      std::bind(&SimManagerNode::publish_state, this));

    // Heartbeat check at 2 Hz
    health_timer_ = this->create_wall_timer(500ms,
      std::bind(&SimManagerNode::check_node_health, this));

    // Own heartbeat at 1 Hz
    own_heartbeat_timer_ = this->create_wall_timer(1s, [this]() {
      auto msg = std_msgs::msg::Header();
      msg.stamp = this->now();
      msg.frame_id = "sim_manager";
      heartbeat_pub_->publish(msg);
    });

    state_ = sim_msgs::msg::SimState::STATE_INIT;
    RCLCPP_INFO(this->get_logger(),
      "sim_manager started [aircraft=%s, state=INIT]", aircraft_id_.c_str());
  }

private:
  // ── State ───────────────────────────────────────────────────────────────
  uint8_t state_ = sim_msgs::msg::SimState::STATE_INIT;
  std::string aircraft_id_;
  double sim_time_sec_ = 0.0;
  double time_scale_ = 1.0;
  InitialConditions current_ic_;
  std::vector<ScenarioEvent> scenario_events_;
  std::vector<std::string> required_nodes_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_heartbeat_;

  // Reset timer
  rclcpp::TimerBase::SharedPtr reset_timer_;

  // ── Publishers ──────────────────────────────────────────────────────────
  rclcpp::Publisher<rosgraph_msgs::msg::Clock>::SharedPtr clock_pub_;
  rclcpp::Publisher<sim_msgs::msg::SimState>::SharedPtr state_pub_;
  rclcpp::Publisher<sim_msgs::msg::SimAlert>::SharedPtr alert_pub_;
  rclcpp::Publisher<sim_msgs::msg::InitialConditions>::SharedPtr ic_pub_;
  rclcpp::Publisher<sim_msgs::msg::ScenarioEvent>::SharedPtr scenario_event_pub_;
  rclcpp::Publisher<std_msgs::msg::Header>::SharedPtr heartbeat_pub_;

  // ── Subscribers ─────────────────────────────────────────────────────────
  rclcpp::Subscription<sim_msgs::msg::SimCommand>::SharedPtr command_sub_;
  std::vector<rclcpp::Subscription<std_msgs::msg::Header>::SharedPtr> heartbeat_subs_;

  // ── Timers ──────────────────────────────────────────────────────────────
  rclcpp::TimerBase::SharedPtr clock_timer_;
  rclcpp::TimerBase::SharedPtr state_timer_;
  rclcpp::TimerBase::SharedPtr health_timer_;
  rclcpp::TimerBase::SharedPtr own_heartbeat_timer_;
  std::chrono::steady_clock::time_point last_clock_tick_ =
    std::chrono::steady_clock::now();

  // ════════════════════════════════════════════════════════════════════════
  //  Aircraft config loading
  // ════════════════════════════════════════════════════════════════════════

  void load_aircraft_config()
  {
    // Try parameter path first, then workspace-relative paths
    std::string config_dir = this->get_parameter("aircraft_config_dir").as_string();
    std::vector<std::string> search_paths;
    if (!config_dir.empty()) {
      search_paths.push_back(config_dir + "/" + aircraft_id_ + "/config.yaml");
    }
    search_paths.push_back("aircraft/" + aircraft_id_ + "/config.yaml");
    // Also try from share directory (installed)
    search_paths.push_back(
      ament_index_cpp::get_package_share_directory("sim_manager") +
      "/aircraft/" + aircraft_id_ + "/config.yaml");

    std::string config_path;
    for (const auto & p : search_paths) {
      if (std::ifstream(p).good()) {
        config_path = p;
        break;
      }
    }

    if (config_path.empty()) {
      RCLCPP_WARN(this->get_logger(),
        "No config.yaml found for aircraft '%s', using defaults", aircraft_id_.c_str());
      // Use sensible defaults
      required_nodes_ = {
        "flight_model_adapter", "atmosphere_node", "input_arbitrator"
      };
      return;
    }

    RCLCPP_INFO(this->get_logger(), "Loading aircraft config: %s", config_path.c_str());
    try {
      YAML::Node config = YAML::LoadFile(config_path);

      if (config["aircraft"]["required_nodes"]) {
        for (const auto & n : config["aircraft"]["required_nodes"]) {
          required_nodes_.push_back(n.as<std::string>());
        }
      }

      // Load default IC from config
      if (config["initial_conditions"]) {
        auto ic = config["initial_conditions"];
        if (ic["latitude_rad"])     current_ic_.latitude_rad     = ic["latitude_rad"].as<double>();
        if (ic["longitude_rad"])    current_ic_.longitude_rad    = ic["longitude_rad"].as<double>();
        if (ic["altitude_msl_m"])   current_ic_.altitude_msl_m   = ic["altitude_msl_m"].as<double>();
        if (ic["heading_rad"])      current_ic_.heading_rad      = ic["heading_rad"].as<double>();
        if (ic["airspeed_ms"])      current_ic_.airspeed_ms      = ic["airspeed_ms"].as<double>();
        if (ic["oat_deviation_k"]) current_ic_.oat_deviation_k  = ic["oat_deviation_k"].as<double>();
        if (ic["qnh_pa"])           current_ic_.qnh_pa           = ic["qnh_pa"].as<double>();
        if (ic["fuel_total_pct"])   current_ic_.fuel_total_pct   = ic["fuel_total_pct"].as<double>();
        if (ic["configuration"])    current_ic_.configuration    = ic["configuration"].as<std::string>();
      }

      RCLCPP_INFO(this->get_logger(),
        "Aircraft config loaded: %zu required nodes", required_nodes_.size());
    } catch (const std::exception & e) {
      RCLCPP_ERROR(this->get_logger(), "Failed to parse config: %s", e.what());
      required_nodes_ = {"flight_model_adapter", "atmosphere_node", "input_arbitrator"};
    }
  }

  // ════════════════════════════════════════════════════════════════════════
  //  Clock tick (50 Hz wall timer)
  // ════════════════════════════════════════════════════════════════════════

  void on_clock_tick()
  {
    auto now_wall = std::chrono::steady_clock::now();
    double dt_wall = std::chrono::duration<double>(now_wall - last_clock_tick_).count();
    last_clock_tick_ = now_wall;

    // Only advance time in RUNNING state
    if (state_ == sim_msgs::msg::SimState::STATE_RUNNING) {
      sim_time_sec_ += dt_wall * time_scale_;
      process_scenario_events();
    }

    // Publish clock in all states (so nodes see time even when frozen)
    rosgraph_msgs::msg::Clock clock_msg;
    int32_t sec = static_cast<int32_t>(sim_time_sec_);
    uint32_t nsec = static_cast<uint32_t>((sim_time_sec_ - sec) * 1e9);
    clock_msg.clock.sec = sec;
    clock_msg.clock.nanosec = nsec;
    clock_pub_->publish(clock_msg);
  }

  // ════════════════════════════════════════════════════════════════════════
  //  State machine
  // ════════════════════════════════════════════════════════════════════════

  static const char * state_name(uint8_t s)
  {
    switch (s) {
      case sim_msgs::msg::SimState::STATE_INIT:      return "INIT";
      case sim_msgs::msg::SimState::STATE_READY:     return "READY";
      case sim_msgs::msg::SimState::STATE_RUNNING:   return "RUNNING";
      case sim_msgs::msg::SimState::STATE_FROZEN:    return "FROZEN";
      case sim_msgs::msg::SimState::STATE_RESETTING: return "RESETTING";
      case sim_msgs::msg::SimState::STATE_SHUTDOWN:  return "SHUTDOWN";
      default: return "UNKNOWN";
    }
  }

  bool transition_to(uint8_t new_state)
  {
    // Validate transition
    using S = sim_msgs::msg::SimState;
    bool valid = false;
    switch (state_) {
      case S::STATE_INIT:
        valid = (new_state == S::STATE_READY || new_state == S::STATE_SHUTDOWN);
        break;
      case S::STATE_READY:
        valid = (new_state == S::STATE_RUNNING || new_state == S::STATE_SHUTDOWN);
        break;
      case S::STATE_RUNNING:
        valid = (new_state == S::STATE_FROZEN ||
                 new_state == S::STATE_RESETTING ||
                 new_state == S::STATE_SHUTDOWN);
        break;
      case S::STATE_FROZEN:
        valid = (new_state == S::STATE_RUNNING ||
                 new_state == S::STATE_RESETTING ||
                 new_state == S::STATE_SHUTDOWN);
        break;
      case S::STATE_RESETTING:
        valid = (new_state == S::STATE_READY || new_state == S::STATE_SHUTDOWN);
        break;
      case S::STATE_SHUTDOWN:
        valid = false;  // terminal state
        break;
    }

    if (!valid) {
      RCLCPP_WARN(this->get_logger(),
        "Invalid state transition: %s -> %s",
        state_name(state_), state_name(new_state));
      return false;
    }

    RCLCPP_INFO(this->get_logger(),
      "State transition: %s -> %s", state_name(state_), state_name(new_state));
    state_ = new_state;
    publish_state();
    return true;
  }

  // ════════════════════════════════════════════════════════════════════════
  //  Command handler
  // ════════════════════════════════════════════════════════════════════════

  void on_command(const sim_msgs::msg::SimCommand::SharedPtr msg)
  {
    using C = sim_msgs::msg::SimCommand;
    using S = sim_msgs::msg::SimState;

    RCLCPP_INFO(this->get_logger(), "Command received: %u", msg->command);

    switch (msg->command) {
      case C::CMD_RUN:
        transition_to(S::STATE_RUNNING);
        break;

      case C::CMD_FREEZE:
        transition_to(S::STATE_FROZEN);
        break;

      case C::CMD_UNFREEZE:
        if (state_ == S::STATE_FROZEN) {
          transition_to(S::STATE_RUNNING);
        }
        break;

      case C::CMD_RESET:
        if (transition_to(S::STATE_RESETTING)) {
          begin_reset();
        }
        break;

      case C::CMD_SET_IC:
        parse_ic_from_json(msg->payload_json);
        break;

      case C::CMD_LOAD_SCENARIO:
        load_scenario(msg->payload_json);
        break;

      case C::CMD_SHUTDOWN:
        transition_to(S::STATE_SHUTDOWN);
        RCLCPP_INFO(this->get_logger(), "Shutdown requested — exiting");
        rclcpp::shutdown();
        break;

      default:
        RCLCPP_WARN(this->get_logger(), "Unknown command: %u", msg->command);
    }
  }

  // ════════════════════════════════════════════════════════════════════════
  //  Initial conditions
  // ════════════════════════════════════════════════════════════════════════

  void parse_ic_from_json(const std::string & payload)
  {
    if (payload.empty()) return;
    try {
      auto j = json::parse(payload);
      if (j.contains("latitude_rad"))     current_ic_.latitude_rad     = j["latitude_rad"].get<double>();
      if (j.contains("longitude_rad"))    current_ic_.longitude_rad    = j["longitude_rad"].get<double>();
      if (j.contains("altitude_msl_m"))   current_ic_.altitude_msl_m   = j["altitude_msl_m"].get<double>();
      if (j.contains("heading_rad"))      current_ic_.heading_rad      = j["heading_rad"].get<double>();
      if (j.contains("airspeed_ms"))      current_ic_.airspeed_ms      = j["airspeed_ms"].get<double>();
      if (j.contains("oat_deviation_k"))  current_ic_.oat_deviation_k  = j["oat_deviation_k"].get<double>();
      if (j.contains("qnh_pa"))           current_ic_.qnh_pa           = j["qnh_pa"].get<double>();
      if (j.contains("fuel_total_pct"))   current_ic_.fuel_total_pct   = j["fuel_total_pct"].get<double>();
      if (j.contains("configuration"))    current_ic_.configuration    = j["configuration"].get<std::string>();
      RCLCPP_INFO(this->get_logger(), "Initial conditions updated from JSON");
    } catch (const json::exception & e) {
      RCLCPP_ERROR(this->get_logger(), "Failed to parse IC JSON: %s", e.what());
    }
  }

  void broadcast_ic()
  {
    auto msg = sim_msgs::msg::InitialConditions();
    msg.header.stamp = this->now();
    msg.latitude_rad     = current_ic_.latitude_rad;
    msg.longitude_rad    = current_ic_.longitude_rad;
    msg.altitude_msl_m   = current_ic_.altitude_msl_m;
    msg.heading_rad      = current_ic_.heading_rad;
    msg.airspeed_ms      = current_ic_.airspeed_ms;
    msg.oat_deviation_k  = current_ic_.oat_deviation_k;
    msg.qnh_pa           = current_ic_.qnh_pa;
    msg.fuel_total_pct   = static_cast<float>(current_ic_.fuel_total_pct);
    msg.configuration    = current_ic_.configuration;
    ic_pub_->publish(msg);
    RCLCPP_INFO(this->get_logger(), "Initial conditions broadcast");
  }

  void begin_reset()
  {
    sim_time_sec_ = 0.0;
    broadcast_ic();

    // Transition to READY after 100ms
    reset_timer_ = this->create_wall_timer(100ms, [this]() {
      reset_timer_->cancel();
      reset_timer_.reset();
      transition_to(sim_msgs::msg::SimState::STATE_READY);
    });
  }

  // ════════════════════════════════════════════════════════════════════════
  //  Scenario loading and execution
  // ════════════════════════════════════════════════════════════════════════

  void load_scenario(const std::string & payload)
  {
    if (payload.empty()) {
      RCLCPP_WARN(this->get_logger(), "CMD_LOAD_SCENARIO: empty payload");
      return;
    }

    std::string scenario_path;
    try {
      auto j = json::parse(payload);
      scenario_path = j.value("path", "");
    } catch (...) {
      // Treat payload as a plain file path
      scenario_path = payload;
    }

    if (scenario_path.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No scenario path provided");
      return;
    }

    try {
      YAML::Node scenario = YAML::LoadFile(scenario_path);
      scenario_events_.clear();

      // Extract IC from scenario if present
      if (scenario["initial_conditions"]) {
        auto ic = scenario["initial_conditions"];
        if (ic["latitude_rad"])     current_ic_.latitude_rad     = ic["latitude_rad"].as<double>();
        if (ic["longitude_rad"])    current_ic_.longitude_rad    = ic["longitude_rad"].as<double>();
        if (ic["altitude_msl_m"])   current_ic_.altitude_msl_m   = ic["altitude_msl_m"].as<double>();
        if (ic["heading_rad"])      current_ic_.heading_rad      = ic["heading_rad"].as<double>();
        if (ic["airspeed_ms"])      current_ic_.airspeed_ms      = ic["airspeed_ms"].as<double>();
        if (ic["oat_deviation_k"]) current_ic_.oat_deviation_k  = ic["oat_deviation_k"].as<double>();
        if (ic["qnh_pa"])           current_ic_.qnh_pa           = ic["qnh_pa"].as<double>();
        if (ic["fuel_total_pct"])   current_ic_.fuel_total_pct   = ic["fuel_total_pct"].as<double>();
        if (ic["configuration"])    current_ic_.configuration    = ic["configuration"].as<std::string>();
      }

      // Load timed events
      if (scenario["events"]) {
        for (const auto & ev : scenario["events"]) {
          ScenarioEvent se;
          se.at_time_s   = ev["at_time_s"].as<double>();
          se.event_id    = ev["event_id"].as<std::string>();
          se.action      = ev["action"].as<std::string>();
          se.params_json = ev["params_json"] ? ev["params_json"].as<std::string>() : "";
          scenario_events_.push_back(se);
        }
        // Sort by time
        std::sort(scenario_events_.begin(), scenario_events_.end(),
          [](const ScenarioEvent & a, const ScenarioEvent & b) {
            return a.at_time_s < b.at_time_s;
          });
      }

      RCLCPP_INFO(this->get_logger(),
        "Scenario loaded: %s (%zu events)", scenario_path.c_str(), scenario_events_.size());

    } catch (const std::exception & e) {
      RCLCPP_ERROR(this->get_logger(), "Failed to load scenario: %s", e.what());
    }
  }

  void process_scenario_events()
  {
    for (auto & ev : scenario_events_) {
      if (!ev.consumed && sim_time_sec_ >= ev.at_time_s) {
        auto msg = sim_msgs::msg::ScenarioEvent();
        msg.header.stamp = this->now();
        msg.event_id    = ev.event_id;
        msg.action      = ev.action;
        msg.params_json = ev.params_json;
        scenario_event_pub_->publish(msg);
        ev.consumed = true;
        RCLCPP_INFO(this->get_logger(),
          "Scenario event fired: %s [%s] at t=%.1fs",
          ev.event_id.c_str(), ev.action.c_str(), ev.at_time_s);
      }
    }
  }

  // ════════════════════════════════════════════════════════════════════════
  //  Node health monitoring
  // ════════════════════════════════════════════════════════════════════════

  void check_node_health()
  {
    auto now = std::chrono::steady_clock::now();

    if (state_ == sim_msgs::msg::SimState::STATE_INIT) {
      // Check if all required nodes are alive → transition to READY
      bool all_alive = true;
      for (const auto & name : required_nodes_) {
        auto it = last_heartbeat_.find(name);
        if (it == last_heartbeat_.end() ||
            it->second == std::chrono::steady_clock::time_point::min()) {
          all_alive = false;
          break;
        }
        auto age = std::chrono::duration<double>(now - it->second).count();
        if (age > 2.0) {
          all_alive = false;
          break;
        }
      }
      if (all_alive && !required_nodes_.empty()) {
        RCLCPP_INFO(this->get_logger(),
          "All %zu required nodes alive — transitioning to READY",
          required_nodes_.size());
        transition_to(sim_msgs::msg::SimState::STATE_READY);
      }
      return;
    }

    // In RUNNING or FROZEN, watch for node timeouts
    if (state_ == sim_msgs::msg::SimState::STATE_RUNNING ||
        state_ == sim_msgs::msg::SimState::STATE_FROZEN)
    {
      for (const auto & name : required_nodes_) {
        auto it = last_heartbeat_.find(name);
        if (it == last_heartbeat_.end()) continue;
        auto age = std::chrono::duration<double>(now - it->second).count();
        if (age > 2.0) {
          publish_alert(sim_msgs::msg::SimAlert::SEVERITY_CRITICAL,
            name, "Node heartbeat timeout (" + std::to_string(age) + "s)");
          if (state_ == sim_msgs::msg::SimState::STATE_RUNNING) {
            RCLCPP_WARN(this->get_logger(),
              "Required node '%s' timed out — freezing", name.c_str());
            transition_to(sim_msgs::msg::SimState::STATE_FROZEN);
          }
        }
      }
    }
  }

  // ════════════════════════════════════════════════════════════════════════
  //  Publishing helpers
  // ════════════════════════════════════════════════════════════════════════

  void publish_state()
  {
    auto msg = sim_msgs::msg::SimState();
    msg.header.stamp = this->now();
    msg.state        = state_;
    msg.aircraft_id  = aircraft_id_;
    msg.sim_time_sec = sim_time_sec_;
    msg.time_scale   = static_cast<float>(time_scale_);
    state_pub_->publish(msg);
  }

  void publish_alert(uint8_t severity, const std::string & source,
                     const std::string & message)
  {
    auto msg = sim_msgs::msg::SimAlert();
    msg.header.stamp = this->now();
    msg.severity = severity;
    msg.source   = source;
    msg.message  = message;
    alert_pub_->publish(msg);
    RCLCPP_WARN(this->get_logger(), "ALERT [%s]: %s", source.c_str(), message.c_str());
  }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<SimManagerNode>());
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("sim_manager"), "Fatal: %s", e.what());
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
