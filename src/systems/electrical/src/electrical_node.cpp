#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <std_msgs/msg/string.hpp>
#include <pluginlib/class_loader.hpp>
#include <sim_interfaces/i_electrical_model.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include <sim_msgs/msg/electrical_state.hpp>
#include <sim_msgs/msg/sim_state.hpp>
#include <sim_msgs/msg/sim_alert.hpp>
#include <sim_msgs/msg/failure_list.hpp>
#include <sim_msgs/msg/failure_injection.hpp>
#include <sim_msgs/msg/panel_controls.hpp>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/flight_model_capabilities.hpp>

#include <fstream>
#include <map>

// ── Simple flat-JSON value extraction (no external library) ──────────────

static std::string extract_json_value(const std::string & json, const std::string & key)
{
  std::string search = "\"" + key + "\":";
  auto pos = json.find(search);
  if (pos == std::string::npos) return "";
  pos += search.size();
  while (pos < json.size() && json[pos] == ' ') pos++;
  if (pos >= json.size()) return "";
  if (json[pos] == '"') {
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
  }
  auto end = json.find_first_of(",}", pos);
  if (end == std::string::npos) return json.substr(pos);
  return json.substr(pos, end - pos);
}

// CB override states from sim_failures
enum class CBOverride { NORMAL, POPPED, LOCKED };

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class ElectricalNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  ElectricalNode()
  : LifecycleNode("sim_electrical", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    this->declare_parameter<std::string>("aircraft_id", "c172");

    auto_start_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      [this]() {
        auto_start_timer_->cancel();
        auto_start_timer_.reset();
        this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE);
        this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE);
      });
    RCLCPP_INFO(this->get_logger(), "sim_electrical constructed (unconfigured)");
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    // Publishers — create alert pub early so we can report config errors
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle_state", 10);
    alert_pub_ = this->create_publisher<sim_msgs::msg::SimAlert>(
      "/sim/alerts", 10);
    elec_state_pub_ = this->create_publisher<sim_msgs::msg::ElectricalState>(
      "/sim/electrical/state", 10);

    // Subscriptions
    sim_state_sub_ = this->create_subscription<sim_msgs::msg::SimState>(
      "/sim/state", 10,
      [this](const sim_msgs::msg::SimState::SharedPtr msg) {
        auto prev = sim_state_;
        sim_state_ = msg->state;

        // Detect RESETTING transition → reload initial conditions
        if (sim_state_ == sim_msgs::msg::SimState::STATE_RESETTING &&
            prev != sim_msgs::msg::SimState::STATE_RESETTING) {
          if (model_) {
            model_->reset();
            RCLCPP_INFO(this->get_logger(), "Electrical reset to initial conditions");
          }
        }
      });

    panel_sub_ = this->create_subscription<sim_msgs::msg::PanelControls>(
      "/sim/controls/panel", 10,
      [this](const sim_msgs::msg::PanelControls::SharedPtr msg) {
        if (!model_) return;
        for (size_t i = 0; i < msg->switch_ids.size() && i < msg->switch_states.size(); ++i) {
          model_->command_switch(msg->switch_ids[i], msg->switch_states[i] ? 1 : 0);
          solver_dirty_ = true;
        }
        // Handle ext_pwr_cart selector: 0 = auto (on_ground), 1 = force on, 2 = force off
        for (size_t i = 0; i < msg->selector_ids.size() && i < msg->selector_values.size(); ++i) {
          if (msg->selector_ids[i] == "ext_pwr_cart") {
            int val = msg->selector_values[i];
            if (val == 0)      ext_pwr_override_ = Override::AUTO;
            else if (val == 1) ext_pwr_override_ = Override::FORCE_ON;
            else               ext_pwr_override_ = Override::FORCE_OFF;
            update_ground_state();
            solver_dirty_ = true;
          }
        }
      });

    flight_model_sub_ = this->create_subscription<sim_msgs::msg::FlightModelState>(
      "/sim/flight_model/state", 10,
      [this](const sim_msgs::msg::FlightModelState::SharedPtr msg) {
        on_ground_ = msg->on_ground;
        update_ground_state();
      });

    failure_sub_ = this->create_subscription<sim_msgs::msg::FailureList>(
      "/sim/failures/active", 10,
      [this](const sim_msgs::msg::FailureList::SharedPtr msg) {
        if (!model_) return;
        std::set<std::string> current_failures(msg->failure_ids.begin(), msg->failure_ids.end());
        for (const auto & prev : active_failures_) {
          if (current_failures.find(prev) == current_failures.end()) {
            model_->apply_failure(prev, false);
          }
        }
        for (size_t i = 0; i < msg->failure_ids.size(); ++i) {
          if (msg->severity[i] > 0) {
            model_->apply_failure(msg->failure_ids[i], true);
          }
        }
        active_failures_ = current_failures;
      });

    // Failure injection commands from sim_failures
    failure_injection_sub_ = this->create_subscription<sim_msgs::msg::FailureInjection>(
      "/sim/failure/electrical_commands",
      rclcpp::QoS(10).reliable(),
      [this](const sim_msgs::msg::FailureInjection::SharedPtr msg) {
        if (!model_) return;
        on_failure_injection(msg);
      });

    // Capabilities subscription (transient_local to receive latched message)
    auto caps_qos = rclcpp::QoS(1).transient_local().reliable();
    caps_sub_ = this->create_subscription<sim_msgs::msg::FlightModelCapabilities>(
      "/sim/flight_model/capabilities", caps_qos,
      [this](const sim_msgs::msg::FlightModelCapabilities::SharedPtr msg) {
        latest_caps_ = msg;
        RCLCPP_INFO(this->get_logger(), "Received FDM capabilities: electrical=%u", msg->electrical);
      });

    // Writeback publisher — used when electrical mode is EXTERNAL_COUPLED
    elec_writeback_pub_ = this->create_publisher<sim_msgs::msg::ElectricalState>(
      "/sim/writeback/electrical", 10);

    // Load aircraft-specific plugin + YAML config
    auto aircraft_id = this->get_parameter("aircraft_id").as_string();
    std::string plugin_name = "aircraft_" + aircraft_id + "::ElectricalModel";
    std::string yaml_path =
      ament_index_cpp::get_package_share_directory("aircraft_" + aircraft_id) +
      "/config/electrical.yaml";

    bool is_reload = (model_ != nullptr);
    if (is_reload) {
      RCLCPP_INFO(this->get_logger(),
        "Reloading electrical config from: %s", yaml_path.c_str());
    }

    try {
      // Check file exists before loading
      {
        std::ifstream test(yaml_path);
        if (!test.good()) {
          std::string err = "Config file not found: " + yaml_path;
          RCLCPP_ERROR(this->get_logger(), "%s", err.c_str());
          publish_alert(sim_msgs::msg::SimAlert::SEVERITY_CRITICAL, err);
          publish_lifecycle_state("unconfigured");
          return CallbackReturn::FAILURE;
        }
      }

      loader_ = std::make_unique<pluginlib::ClassLoader<sim_interfaces::IElectricalModel>>(
        "sim_interfaces", "sim_interfaces::IElectricalModel");
      model_ = loader_->createSharedInstance(plugin_name);
      model_->configure(yaml_path);

      // Default engine N2 to running (75%) — until engine systems node feeds real data
      model_->set_engine_n2({75.0});

    } catch (const std::exception & e) {
      std::string err = std::string("Failed to configure electrical: ") + e.what();
      RCLCPP_ERROR(this->get_logger(), "%s", err.c_str());
      publish_alert(sim_msgs::msg::SimAlert::SEVERITY_CRITICAL, err);
      model_.reset();
      loader_.reset();
      publish_lifecycle_state("unconfigured");
      return CallbackReturn::FAILURE;
    }

    RCLCPP_INFO(this->get_logger(), "sim_electrical configured — plugin: %s, config: %s",
      plugin_name.c_str(), yaml_path.c_str());
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    elec_state_pub_->on_activate();

    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::String();
        msg.data = this->get_name();
        heartbeat_pub_->publish(msg);
      });

    // 50 Hz update timer (20ms)
    constexpr double dt_sec = 0.02;
    update_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(20),
      [this, dt_sec]() {
        if (!model_) return;

        // Gate: if FDM handles electrical natively, don't run our solver
        bool mode_is_ours = !latest_caps_ ||
          latest_caps_->electrical != sim_msgs::msg::FlightModelCapabilities::FDM_NATIVE;

        if (!mode_is_ours) return;

        bool running = (sim_state_ == sim_msgs::msg::SimState::STATE_INIT ||
                        sim_state_ == sim_msgs::msg::SimState::STATE_READY ||
                        sim_state_ == sim_msgs::msg::SimState::STATE_RUNNING);

        if (running) {
          // Full update — SOC drains, all loads/sources/voltages computed
          model_->update(dt_sec);
        } else if (solver_dirty_) {
          // Frozen but switch changed — run solver once with dt=0 (no SOC drain)
          model_->update(0.0);
          solver_dirty_ = false;
        }
        // Always publish — IOS needs live data even when frozen

        auto snap = model_->get_snapshot();
        auto msg = sim_msgs::msg::ElectricalState();
        msg.header.stamp = this->now();

        for (const auto & b : snap.buses) {
          msg.bus_names.push_back(b.name);
          msg.bus_voltages.push_back(b.voltage);
          msg.bus_powered.push_back(b.powered);
        }
        for (const auto & s : snap.sources) {
          msg.source_names.push_back(s.name);
          msg.source_active.push_back(s.active);
          msg.source_voltages.push_back(s.voltage);
          msg.source_currents.push_back(s.current);
        }
        for (const auto & l : snap.loads) {
          msg.load_names.push_back(l.name);
          msg.load_powered.push_back(l.powered);
          msg.load_currents.push_back(l.current);
        }
        for (const auto & sw : snap.switches) {
          msg.switch_ids.push_back(sw.id);
          msg.switch_labels.push_back(sw.label);
          msg.switch_closed.push_back(sw.closed);
        }
        for (const auto & c : snap.cbs) {
          msg.cb_names.push_back(c.name);
          msg.cb_closed.push_back(c.closed);
          msg.cb_tripped.push_back(c.tripped);
        }
        msg.total_load_amps = snap.total_load_amps;
        msg.battery_soc_pct = snap.battery_soc_pct;
        msg.master_bus_voltage = snap.master_bus_voltage;
        msg.avionics_bus_powered = snap.avionics_bus_powered;
        msg.essential_bus_powered = snap.essential_bus_powered;

        elec_state_pub_->publish(msg);

        // Writeback to FDM when EXTERNAL_COUPLED
        if (latest_caps_ &&
            latest_caps_->electrical == sim_msgs::msg::FlightModelCapabilities::EXTERNAL_COUPLED) {
          elec_writeback_pub_->publish(msg);
        }
      });

    RCLCPP_INFO(this->get_logger(), "sim_electrical activated");
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    heartbeat_timer_.reset();
    update_timer_.reset();
    RCLCPP_INFO(this->get_logger(), "sim_electrical deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    heartbeat_pub_.reset();
    lifecycle_state_pub_.reset();
    alert_pub_.reset();
    elec_state_pub_.reset();
    sim_state_sub_.reset();
    panel_sub_.reset();
    flight_model_sub_.reset();
    failure_sub_.reset();
    failure_injection_sub_.reset();
    caps_sub_.reset();
    elec_writeback_pub_.reset();
    latest_caps_.reset();
    model_.reset();
    loader_.reset();
    active_failures_.clear();
    failed_components_.clear();
    cb_overrides_.clear();
    RCLCPP_INFO(this->get_logger(), "sim_electrical cleaned up");
    publish_lifecycle_state("unconfigured");
    return CallbackReturn::SUCCESS;
  }

private:
  void publish_lifecycle_state(const std::string & state)
  {
    if (lifecycle_state_pub_) {
      auto msg = std_msgs::msg::String();
      msg.data = std::string(this->get_name()) + ":" + state;
      lifecycle_state_pub_->publish(msg);
    }
  }

  void on_failure_injection(const sim_msgs::msg::FailureInjection::SharedPtr & msg)
  {
    if (msg->method == "set_electrical_component_failed") {
      std::string component_id = extract_json_value(msg->params_json, "component_id");
      if (component_id.empty()) return;

      if (msg->active) {
        failed_components_.insert(component_id);
        model_->apply_failure(component_id + "/fail", true);
      } else {
        failed_components_.erase(component_id);
        model_->apply_failure(component_id + "/fail", false);
      }
      solver_dirty_ = true;
      RCLCPP_INFO(this->get_logger(), "Electrical component %s: %s",
        component_id.c_str(), msg->active ? "FAILED" : "RESTORED");

    } else if (msg->method == "set_circuit_breaker_state") {
      std::string cb_id = extract_json_value(msg->params_json, "component_id");
      std::string state = extract_json_value(msg->params_json, "state");
      if (cb_id.empty()) return;

      if (state == "popped") {
        cb_overrides_[cb_id] = CBOverride::POPPED;
        model_->command_switch(cb_id, 0);  // open the CB
      } else if (state == "locked") {
        cb_overrides_[cb_id] = CBOverride::LOCKED;
        model_->command_switch(cb_id, 0);  // open and lock
      } else {
        // normal — remove override, allow solver to control
        cb_overrides_.erase(cb_id);
      }
      solver_dirty_ = true;
      RCLCPP_INFO(this->get_logger(), "CB %s override: %s",
        cb_id.c_str(), state.c_str());

    } else {
      RCLCPP_WARN(this->get_logger(), "Unknown electrical failure method: %s",
        msg->method.c_str());
    }
  }

  void update_ground_state()
  {
    if (!model_) return;
    bool ext_pwr;
    switch (ext_pwr_override_) {
      case Override::FORCE_ON:  ext_pwr = true; break;
      case Override::FORCE_OFF: ext_pwr = false; break;
      default:                  ext_pwr = on_ground_; break;  // AUTO: available when on ground
    }
    model_->set_ground_state(on_ground_, ext_pwr);
  }

  void publish_alert(uint8_t severity, const std::string & message)
  {
    if (alert_pub_) {
      auto msg = sim_msgs::msg::SimAlert();
      msg.header.stamp = this->now();
      msg.severity = severity;
      msg.source = this->get_name();
      msg.message = message;
      alert_pub_->publish(msg);
    }
  }

  // Publishers
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr heartbeat_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr lifecycle_state_pub_;
  rclcpp::Publisher<sim_msgs::msg::SimAlert>::SharedPtr alert_pub_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::ElectricalState>::SharedPtr elec_state_pub_;

  // Subscriptions
  rclcpp::Subscription<sim_msgs::msg::SimState>::SharedPtr sim_state_sub_;
  rclcpp::Subscription<sim_msgs::msg::PanelControls>::SharedPtr panel_sub_;
  rclcpp::Subscription<sim_msgs::msg::FlightModelState>::SharedPtr flight_model_sub_;
  rclcpp::Subscription<sim_msgs::msg::FailureList>::SharedPtr failure_sub_;
  rclcpp::Subscription<sim_msgs::msg::FailureInjection>::SharedPtr failure_injection_sub_;
  rclcpp::Subscription<sim_msgs::msg::FlightModelCapabilities>::SharedPtr caps_sub_;

  // Capabilities writeback
  rclcpp::Publisher<sim_msgs::msg::ElectricalState>::SharedPtr elec_writeback_pub_;
  sim_msgs::msg::FlightModelCapabilities::SharedPtr latest_caps_;

  // Timers
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;
  rclcpp::TimerBase::SharedPtr update_timer_;

  // Plugin
  std::unique_ptr<pluginlib::ClassLoader<sim_interfaces::IElectricalModel>> loader_;
  std::shared_ptr<sim_interfaces::IElectricalModel> model_;

  // State
  uint8_t sim_state_ = sim_msgs::msg::SimState::STATE_INIT;
  bool solver_dirty_ = false;
  bool on_ground_ = true;
  enum class Override { AUTO, FORCE_ON, FORCE_OFF };
  Override ext_pwr_override_ = Override::AUTO;
  std::set<std::string> active_failures_;
  std::set<std::string> failed_components_;
  std::map<std::string, CBOverride> cb_overrides_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ElectricalNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
