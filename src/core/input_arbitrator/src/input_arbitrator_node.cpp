#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <lifecycle_msgs/msg/state.hpp>
#include <std_msgs/msg/header.hpp>
#include <std_msgs/msg/string.hpp>

#include <sim_msgs/msg/raw_flight_controls.hpp>
#include <sim_msgs/msg/raw_engine_controls.hpp>
#include <sim_msgs/msg/raw_avionics_controls.hpp>
#include <sim_msgs/msg/panel_controls.hpp>
#include <sim_msgs/msg/flight_controls.hpp>
#include <sim_msgs/msg/engine_controls.hpp>
#include <sim_msgs/msg/avionics_controls.hpp>
#include <sim_msgs/msg/device_heartbeat.hpp>
#include <sim_msgs/msg/arbitration_state.hpp>
#include <sim_msgs/msg/sim_state.hpp>
#include <sim_msgs/msg/sim_alert.hpp>

#include <map>
#include <unordered_map>
#include <string>
#include <chrono>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class InputArbitratorNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  static constexpr uint8_t SOURCE_FROZEN     = 0;
  static constexpr uint8_t SOURCE_HARDWARE   = 1;
  static constexpr uint8_t SOURCE_VIRTUAL    = 2;
  static constexpr uint8_t SOURCE_INSTRUCTOR = 3;

  InputArbitratorNode()
  : LifecycleNode("input_arbitrator", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    this->declare_parameter("hardware_timeout_ms", 500);
    this->declare_parameter("update_rate_hz", 50);

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
    RCLCPP_INFO(this->get_logger(), "input_arbitrator constructed (unconfigured)");
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    hardware_timeout_ms_ = this->get_parameter("hardware_timeout_ms").as_int();
    update_rate_hz_ = this->get_parameter("update_rate_hz").as_int();

    // Initialize all channels to VIRTUAL source, hardware unhealthy
    flight_source_   = SOURCE_VIRTUAL;
    engine_source_   = SOURCE_VIRTUAL;
    avionics_source_ = SOURCE_VIRTUAL;
    panel_source_    = SOURCE_VIRTUAL;

    hw_flight_healthy_   = false;
    hw_engine_healthy_   = false;
    hw_avionics_healthy_ = false;
    hw_panel_healthy_    = false;

    has_inst_flight_   = false;
    has_inst_engine_   = false;
    has_inst_avionics_ = false;
    switch_controls_.clear();
    selector_controls_.clear();
    pot_map_.clear();
    encoder_abs_map_.clear();

    sim_frozen_ = false;
    last_hw_heartbeat_ = rclcpp::Time(0, 0, RCL_ROS_TIME);

    // Clear stored messages
    latest_hw_flight_   = sim_msgs::msg::RawFlightControls();
    latest_virt_flight_ = sim_msgs::msg::RawFlightControls();
    latest_inst_flight_ = sim_msgs::msg::RawFlightControls();

    latest_hw_engine_   = sim_msgs::msg::RawEngineControls();
    latest_virt_engine_ = sim_msgs::msg::RawEngineControls();
    latest_inst_engine_ = sim_msgs::msg::RawEngineControls();

    latest_hw_avionics_   = sim_msgs::msg::RawAvionicsControls();
    latest_virt_avionics_ = sim_msgs::msg::RawAvionicsControls();
    latest_inst_avionics_ = sim_msgs::msg::RawAvionicsControls();

    frozen_flight_   = sim_msgs::msg::FlightControls();
    frozen_engine_   = sim_msgs::msg::EngineControls();
    frozen_avionics_ = sim_msgs::msg::AvionicsControls();

    current_panel_  = sim_msgs::msg::PanelControls();
    last_published_panel_ = sim_msgs::msg::PanelControls();

    auto qos = rclcpp::QoS(10);

    // --- Subscriptions: flight controls ---
    sub_hw_flight_ = this->create_subscription<sim_msgs::msg::RawFlightControls>(
      "/devices/hardware/controls/flight", qos,
      [this](sim_msgs::msg::RawFlightControls::SharedPtr msg) {
        latest_hw_flight_ = *msg;
      });
    sub_virt_flight_ = this->create_subscription<sim_msgs::msg::RawFlightControls>(
      "/devices/virtual/controls/flight", qos,
      [this](sim_msgs::msg::RawFlightControls::SharedPtr msg) {
        latest_virt_flight_ = *msg;
      });
    sub_inst_flight_ = this->create_subscription<sim_msgs::msg::RawFlightControls>(
      "/devices/instructor/controls/flight", qos,
      [this](sim_msgs::msg::RawFlightControls::SharedPtr msg) {
        latest_inst_flight_ = *msg;
        has_inst_flight_ = true;
      });

    // --- Subscriptions: engine controls ---
    sub_hw_engine_ = this->create_subscription<sim_msgs::msg::RawEngineControls>(
      "/devices/hardware/controls/engine", qos,
      [this](sim_msgs::msg::RawEngineControls::SharedPtr msg) {
        latest_hw_engine_ = *msg;
      });
    sub_virt_engine_ = this->create_subscription<sim_msgs::msg::RawEngineControls>(
      "/devices/virtual/controls/engine", qos,
      [this](sim_msgs::msg::RawEngineControls::SharedPtr msg) {
        latest_virt_engine_ = *msg;
      });
    sub_inst_engine_ = this->create_subscription<sim_msgs::msg::RawEngineControls>(
      "/devices/instructor/controls/engine", qos,
      [this](sim_msgs::msg::RawEngineControls::SharedPtr msg) {
        latest_inst_engine_ = *msg;
        has_inst_engine_ = true;
      });

    // --- Subscriptions: avionics controls ---
    sub_hw_avionics_ = this->create_subscription<sim_msgs::msg::RawAvionicsControls>(
      "/devices/hardware/controls/avionics", qos,
      [this](sim_msgs::msg::RawAvionicsControls::SharedPtr msg) {
        latest_hw_avionics_ = *msg;
      });
    sub_virt_avionics_ = this->create_subscription<sim_msgs::msg::RawAvionicsControls>(
      "/devices/virtual/controls/avionics", qos,
      [this](sim_msgs::msg::RawAvionicsControls::SharedPtr msg) {
        latest_virt_avionics_ = *msg;
      });
    sub_inst_avionics_ = this->create_subscription<sim_msgs::msg::RawAvionicsControls>(
      "/devices/instructor/controls/avionics", qos,
      [this](sim_msgs::msg::RawAvionicsControls::SharedPtr msg) {
        latest_inst_avionics_ = *msg;
        has_inst_avionics_ = true;
      });

    // --- Subscriptions: panel controls ---
    sub_hw_panel_ = this->create_subscription<sim_msgs::msg::PanelControls>(
      "/devices/hardware/panel", qos,
      [this](sim_msgs::msg::PanelControls::SharedPtr msg) {
        on_panel_received(*msg, SOURCE_HARDWARE);
      });
    sub_virt_panel_ = this->create_subscription<sim_msgs::msg::PanelControls>(
      "/devices/virtual/panel", qos,
      [this](sim_msgs::msg::PanelControls::SharedPtr msg) {
        on_panel_received(*msg, SOURCE_VIRTUAL);
      });
    sub_inst_panel_ = this->create_subscription<sim_msgs::msg::PanelControls>(
      "/devices/instructor/panel", qos,
      [this](sim_msgs::msg::PanelControls::SharedPtr msg) {
        on_panel_received(*msg, SOURCE_INSTRUCTOR);
      });

    // --- Subscription: hardware heartbeat ---
    sub_heartbeat_ = this->create_subscription<sim_msgs::msg::DeviceHeartbeat>(
      "/devices/hardware/heartbeat", qos,
      [this](sim_msgs::msg::DeviceHeartbeat::SharedPtr msg) {
        on_heartbeat(*msg);
      });

    // --- Subscription: sim state (for FROZEN detection) ---
    sub_sim_state_ = this->create_subscription<sim_msgs::msg::SimState>(
      "/sim/state", qos,
      [this](sim_msgs::msg::SimState::SharedPtr msg) {
        on_sim_state(*msg);
      });

    // --- Publishers ---
    pub_flight_ = this->create_publisher<sim_msgs::msg::FlightControls>(
      "/sim/controls/flight", 10);
    pub_engine_ = this->create_publisher<sim_msgs::msg::EngineControls>(
      "/sim/controls/engine", 10);
    pub_avionics_ = this->create_publisher<sim_msgs::msg::AvionicsControls>(
      "/sim/controls/avionics", 10);
    pub_panel_ = this->create_publisher<sim_msgs::msg::PanelControls>(
      "/sim/controls/panel", 10);
    pub_arbitration_ = this->create_publisher<sim_msgs::msg::ArbitrationState>(
      "/sim/controls/arbitration", 10);
    pub_alert_ = this->create_publisher<sim_msgs::msg::SimAlert>(
      "/sim/alerts", 10);
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle_state", 10);

    RCLCPP_INFO(this->get_logger(), "input_arbitrator configured (timeout=%dms, rate=%dHz)",
      hardware_timeout_ms_, update_rate_hz_);
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    pub_flight_->on_activate();
    pub_engine_->on_activate();
    pub_avionics_->on_activate();
    pub_panel_->on_activate();
    pub_arbitration_->on_activate();
    pub_alert_->on_activate();

    int period_ms = 1000 / update_rate_hz_;
    controls_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(period_ms),
      [this]() { publish_controls(); });

    arbitration_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      [this]() { publish_arbitration_state(); });

    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::String();
        msg.data = this->get_name();
        heartbeat_pub_->publish(msg);
      });

    RCLCPP_INFO(this->get_logger(), "input_arbitrator activated");
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    controls_timer_.reset();
    arbitration_timer_.reset();
    heartbeat_timer_.reset();

    pub_flight_->on_deactivate();
    pub_engine_->on_deactivate();
    pub_avionics_->on_deactivate();
    pub_panel_->on_deactivate();
    pub_arbitration_->on_deactivate();
    pub_alert_->on_deactivate();

    RCLCPP_INFO(this->get_logger(), "input_arbitrator deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    sub_hw_flight_.reset();
    sub_virt_flight_.reset();
    sub_inst_flight_.reset();
    sub_hw_engine_.reset();
    sub_virt_engine_.reset();
    sub_inst_engine_.reset();
    sub_hw_avionics_.reset();
    sub_virt_avionics_.reset();
    sub_inst_avionics_.reset();
    sub_hw_panel_.reset();
    sub_virt_panel_.reset();
    sub_inst_panel_.reset();
    sub_heartbeat_.reset();
    sub_sim_state_.reset();

    pub_flight_.reset();
    pub_engine_.reset();
    pub_avionics_.reset();
    pub_panel_.reset();
    pub_arbitration_.reset();
    pub_alert_.reset();
    heartbeat_pub_.reset();
    lifecycle_state_pub_.reset();

    RCLCPP_INFO(this->get_logger(), "input_arbitrator cleaned up");
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

  // ──────────────────────────────────────────────────────────────────
  // Source selection — returns the effective source for a channel
  // Priority: INSTRUCTOR > HARDWARE > VIRTUAL
  // FROZEN overrides all (except instructor panel)
  // ──────────────────────────────────────────────────────────────────
  uint8_t resolve_source(bool has_instructor, bool hw_healthy) const
  {
    if (has_instructor) return SOURCE_INSTRUCTOR;
    if (hw_healthy)     return SOURCE_HARDWARE;
    return SOURCE_VIRTUAL;
  }

  // ──────────────────────────────────────────────────────────────────
  // Hardware heartbeat handler
  // ──────────────────────────────────────────────────────────────────
  void on_heartbeat(const sim_msgs::msg::DeviceHeartbeat & msg)
  {
    last_hw_heartbeat_ = this->now();

    bool prev_flight   = hw_flight_healthy_;
    bool prev_engine   = hw_engine_healthy_;
    bool prev_avionics = hw_avionics_healthy_;
    bool prev_panel    = hw_panel_healthy_;

    hw_flight_healthy_   = msg.flight_ok;
    hw_engine_healthy_   = msg.engine_ok;
    hw_avionics_healthy_ = msg.avionics_ok;
    hw_panel_healthy_    = msg.panel_ok;

    // Check for per-channel transitions from healthy→unhealthy
    if (prev_flight && !hw_flight_healthy_) {
      publish_fallback_alert("flight");
    }
    if (prev_engine && !hw_engine_healthy_) {
      publish_fallback_alert("engine");
    }
    if (prev_avionics && !hw_avionics_healthy_) {
      publish_fallback_alert("avionics");
    }
    if (prev_panel && !hw_panel_healthy_) {
      publish_fallback_alert("panel");
    }
  }

  // ──────────────────────────────────────────────────────────────────
  // Hardware heartbeat timeout check — called every control tick
  // ──────────────────────────────────────────────────────────────────
  void check_hardware_timeout()
  {
    if (last_hw_heartbeat_.nanoseconds() == 0) {
      // Never received a heartbeat — stay unhealthy
      return;
    }

    auto now = this->now();
    auto elapsed_ms = (now - last_hw_heartbeat_).nanoseconds() / 1000000;

    if (elapsed_ms > hardware_timeout_ms_) {
      bool was_any_healthy = hw_flight_healthy_ || hw_engine_healthy_ ||
                             hw_avionics_healthy_ || hw_panel_healthy_;

      if (hw_flight_healthy_) {
        hw_flight_healthy_ = false;
        publish_fallback_alert("flight");
      }
      if (hw_engine_healthy_) {
        hw_engine_healthy_ = false;
        publish_fallback_alert("engine");
      }
      if (hw_avionics_healthy_) {
        hw_avionics_healthy_ = false;
        publish_fallback_alert("avionics");
      }
      if (hw_panel_healthy_) {
        hw_panel_healthy_ = false;
        publish_fallback_alert("panel");
      }

      if (was_any_healthy) {
        RCLCPP_WARN(this->get_logger(),
          "Hardware heartbeat timeout (%dms) — all channels marked unhealthy",
          hardware_timeout_ms_);
      }
    }
  }

  void publish_fallback_alert(const std::string & channel)
  {
    RCLCPP_WARN(this->get_logger(),
      "Hardware %s channel unhealthy — falling back to VIRTUAL", channel.c_str());

    auto alert = sim_msgs::msg::SimAlert();
    alert.header.stamp = this->now();
    alert.severity = sim_msgs::msg::SimAlert::SEVERITY_WARNING;
    alert.source = "input_arbitrator";
    alert.message = "Hardware " + channel + " channel unhealthy — auto-fallback to VIRTUAL";
    pub_alert_->publish(alert);
  }

  // ──────────────────────────────────────────────────────────────────
  // Sim state handler — detect FROZEN
  // ──────────────────────────────────────────────────────────────────
  void on_sim_state(const sim_msgs::msg::SimState & msg)
  {
    bool was_frozen = sim_frozen_;
    sim_frozen_ = (msg.state == sim_msgs::msg::SimState::STATE_FROZEN);

    if (sim_frozen_ && !was_frozen) {
      // Entering frozen: capture current output as frozen values
      frozen_flight_   = last_published_flight_;
      frozen_engine_   = last_published_engine_;
      frozen_avionics_ = last_published_avionics_;
      RCLCPP_INFO(this->get_logger(), "FROZEN — holding all control values");
    } else if (!sim_frozen_ && was_frozen) {
      RCLCPP_INFO(this->get_logger(), "UNFROZEN — resuming live control input");
    }
  }

  // ──────────────────────────────────────────────────────────────────
  // Per-switch / per-selector force state
  // ──────────────────────────────────────────────────────────────────
  struct SwitchControlState {
    bool forced = false;
    bool force_value = false;
    bool has_virtual = false;
    bool virtual_value = false;
    bool has_hardware = false;
    bool hardware_value = false;
  };

  struct SelectorControlState {
    bool forced = false;
    int32_t force_value = 0;
    bool has_virtual = false;
    int32_t virtual_value = 0;
    bool has_hardware = false;
    int32_t hardware_value = 0;
  };

  // ──────────────────────────────────────────────────────────────────
  // Panel on-change handler — per-switch force model
  // ──────────────────────────────────────────────────────────────────
  void on_panel_received(const sim_msgs::msg::PanelControls & msg, uint8_t source)
  {
    // During FROZEN, only instructor panel commands pass through
    if (sim_frozen_ && source != SOURCE_INSTRUCTOR) {
      return;
    }

    bool changed = false;

    // --- Process switches ---
    // Iterate over switch_ids (not bounded by switch_states) so force-only
    // release messages (switch_ids + switch_forced, no switch_states) work.
    for (size_t i = 0; i < msg.switch_ids.size(); ++i) {
      const auto & id = msg.switch_ids[i];
      bool has_state = (i < msg.switch_states.size());
      auto & ctrl = switch_controls_[id];

      if (source == SOURCE_INSTRUCTOR) {
        bool has_force_flag = (i < msg.switch_forced.size());
        if (has_force_flag && !msg.switch_forced[i]) {
          // RELEASE: give back to cockpit/hardware — don't touch value
          ctrl.forced = false;
          RCLCPP_INFO(get_logger(), "Panel RELEASE: id=%s", id.c_str());
        } else if (has_force_flag && msg.switch_forced[i] && has_state) {
          // EXPLICIT FORCE: lock switch at this value
          ctrl.forced = true;
          ctrl.force_value = msg.switch_states[i];
          RCLCPP_INFO(get_logger(), "Panel FORCE: id=%s state=%d", id.c_str(), (int)msg.switch_states[i]);
        } else if (!has_force_flag && has_state) {
          // No force flag: instructor sets value without forcing
          ctrl.has_virtual = true;
          ctrl.virtual_value = msg.switch_states[i];
          RCLCPP_INFO(get_logger(), "Panel SET (no force): id=%s state=%d", id.c_str(), (int)msg.switch_states[i]);
        }
        changed = true;
      } else if (has_state) {
        bool state = msg.switch_states[i];
        if (source == SOURCE_VIRTUAL) {
          ctrl.has_virtual = true;
          if (ctrl.virtual_value != state) changed = true;
          ctrl.virtual_value = state;
        } else if (source == SOURCE_HARDWARE) {
          ctrl.has_hardware = true;
          if (ctrl.hardware_value != state) changed = true;
          ctrl.hardware_value = state;
        }
      }
    }

    // --- Process selectors ---
    for (size_t i = 0; i < msg.selector_ids.size(); ++i) {
      const auto & id = msg.selector_ids[i];
      bool has_val = (i < msg.selector_values.size());
      auto & ctrl = selector_controls_[id];

      if (source == SOURCE_INSTRUCTOR) {
        bool has_force_flag = (i < msg.selector_forced.size());
        if (has_force_flag && !msg.selector_forced[i]) {
          ctrl.forced = false;
        } else if (has_force_flag && msg.selector_forced[i] && has_val) {
          ctrl.forced = true;
          ctrl.force_value = msg.selector_values[i];
        } else if (!has_force_flag && has_val) {
          ctrl.has_virtual = true;
          ctrl.virtual_value = msg.selector_values[i];
        }
        changed = true;
      } else if (has_val) {
        int32_t val = msg.selector_values[i];
        if (source == SOURCE_VIRTUAL) {
          ctrl.has_virtual = true;
          if (ctrl.virtual_value != val) changed = true;
          ctrl.virtual_value = val;
        } else if (source == SOURCE_HARDWARE) {
          ctrl.has_hardware = true;
          if (ctrl.hardware_value != val) changed = true;
          ctrl.hardware_value = val;
        }
      }
    }

    // --- Process pots/encoders (simple merge, no force) ---
    for (size_t i = 0; i < msg.pot_ids.size() && i < msg.pot_values.size(); ++i) {
      pot_map_[msg.pot_ids[i]] = msg.pot_values[i];
      changed = true;
    }
    for (size_t i = 0; i < msg.encoder_abs_ids.size() && i < msg.encoder_abs_values.size(); ++i) {
      encoder_abs_map_[msg.encoder_abs_ids[i]] = msg.encoder_abs_values[i];
      changed = true;
    }

    if (changed) {
      publish_effective_panel();
    }
  }

  void publish_effective_panel()
  {
    auto output = sim_msgs::msg::PanelControls();
    output.header.stamp = this->now();

    // Resolve effective switch values
    for (const auto & [id, ctrl] : switch_controls_) {
      bool effective;
      if (ctrl.forced) {
        effective = ctrl.force_value;
      } else if (ctrl.has_hardware && hw_panel_healthy_) {
        effective = ctrl.hardware_value;
      } else if (ctrl.has_virtual) {
        effective = ctrl.virtual_value;
      } else {
        continue;  // no value from any source
      }
      output.switch_ids.push_back(id);
      output.switch_states.push_back(effective);
    }

    // Resolve effective selector values
    for (const auto & [id, ctrl] : selector_controls_) {
      int32_t effective;
      if (ctrl.forced) {
        effective = ctrl.force_value;
      } else if (ctrl.has_hardware && hw_panel_healthy_) {
        effective = ctrl.hardware_value;
      } else if (ctrl.has_virtual) {
        effective = ctrl.virtual_value;
      } else {
        continue;
      }
      output.selector_ids.push_back(id);
      output.selector_values.push_back(effective);
    }

    // Pots and encoders — pass through
    for (const auto & [id, val] : pot_map_) {
      output.pot_ids.push_back(id);
      output.pot_values.push_back(val);
    }
    for (const auto & [id, val] : encoder_abs_map_) {
      output.encoder_abs_ids.push_back(id);
      output.encoder_abs_values.push_back(val);
    }

    current_panel_ = output;
    last_published_panel_ = output;
    pub_panel_->publish(output);
  }

  // ──────────────────────────────────────────────────────────────────
  // Copy Raw* → arbitrated output messages
  // ──────────────────────────────────────────────────────────────────
  static sim_msgs::msg::FlightControls to_flight(
    const sim_msgs::msg::RawFlightControls & raw, const rclcpp::Time & stamp)
  {
    sim_msgs::msg::FlightControls out;
    out.header.stamp = stamp;
    out.aileron_norm      = raw.aileron_norm;
    out.elevator_norm     = raw.elevator_norm;
    out.rudder_norm       = raw.rudder_norm;
    out.collective_norm   = raw.collective_norm;
    out.trim_aileron_norm = raw.trim_aileron_norm;
    out.trim_elevator_norm = raw.trim_elevator_norm;
    out.trim_rudder_norm  = raw.trim_rudder_norm;
    out.brake_left_norm   = raw.brake_left_norm;
    out.brake_right_norm  = raw.brake_right_norm;
    return out;
  }

  static sim_msgs::msg::EngineControls to_engine(
    const sim_msgs::msg::RawEngineControls & raw, const rclcpp::Time & stamp)
  {
    sim_msgs::msg::EngineControls out;
    out.header.stamp  = stamp;
    out.throttle_norm     = raw.throttle_norm;
    out.mixture_norm      = raw.mixture_norm;
    out.condition_norm    = raw.condition_norm;
    out.prop_lever_norm   = raw.prop_lever_norm;
    out.magneto_left  = raw.magneto_left;
    out.magneto_right = raw.magneto_right;
    out.starter       = raw.starter;
    return out;
  }

  static sim_msgs::msg::AvionicsControls to_avionics(
    const sim_msgs::msg::RawAvionicsControls & raw, const rclcpp::Time & stamp)
  {
    sim_msgs::msg::AvionicsControls out;
    out.header.stamp      = stamp;
    out.com1_freq_mhz    = raw.com1_freq_mhz;
    out.com2_freq_mhz    = raw.com2_freq_mhz;
    out.com3_freq_mhz    = raw.com3_freq_mhz;
    out.nav1_freq_mhz    = raw.nav1_freq_mhz;
    out.nav2_freq_mhz    = raw.nav2_freq_mhz;
    out.obs1_deg          = raw.obs1_deg;
    out.obs2_deg          = raw.obs2_deg;
    out.adf1_freq_khz    = raw.adf1_freq_khz;
    out.adf2_freq_khz    = raw.adf2_freq_khz;
    out.transponder_code  = raw.transponder_code;
    out.transponder_mode  = raw.transponder_mode;
    out.dme_source        = raw.dme_source;
    out.tacan_channel     = raw.tacan_channel;
    out.tacan_band        = raw.tacan_band;
    out.gps_source        = raw.gps_source;
    return out;
  }

  // ──────────────────────────────────────────────────────────────────
  // 50 Hz publish — select source, publish continuous controls
  // ──────────────────────────────────────────────────────────────────
  void publish_controls()
  {
    check_hardware_timeout();

    auto now = this->now();

    // --- Flight controls ---
    flight_source_ = resolve_source(has_inst_flight_, hw_flight_healthy_);
    if (sim_frozen_) {
      flight_source_ = SOURCE_FROZEN;
      pub_flight_->publish(frozen_flight_);
    } else {
      sim_msgs::msg::FlightControls flight_out;
      switch (flight_source_) {
        case SOURCE_INSTRUCTOR:
          flight_out = to_flight(latest_inst_flight_, now); break;
        case SOURCE_HARDWARE:
          flight_out = to_flight(latest_hw_flight_, now); break;
        default:
          flight_out = to_flight(latest_virt_flight_, now); break;
      }
      last_published_flight_ = flight_out;
      pub_flight_->publish(flight_out);
    }

    // --- Engine controls ---
    engine_source_ = resolve_source(has_inst_engine_, hw_engine_healthy_);
    if (sim_frozen_) {
      engine_source_ = SOURCE_FROZEN;
      pub_engine_->publish(frozen_engine_);
    } else {
      sim_msgs::msg::EngineControls engine_out;
      switch (engine_source_) {
        case SOURCE_INSTRUCTOR:
          engine_out = to_engine(latest_inst_engine_, now); break;
        case SOURCE_HARDWARE:
          engine_out = to_engine(latest_hw_engine_, now); break;
        default:
          engine_out = to_engine(latest_virt_engine_, now); break;
      }
      last_published_engine_ = engine_out;
      pub_engine_->publish(engine_out);
    }

    // --- Avionics controls ---
    avionics_source_ = resolve_source(has_inst_avionics_, hw_avionics_healthy_);
    if (sim_frozen_) {
      avionics_source_ = SOURCE_FROZEN;
      pub_avionics_->publish(frozen_avionics_);
    } else {
      sim_msgs::msg::AvionicsControls avionics_out;
      switch (avionics_source_) {
        case SOURCE_INSTRUCTOR:
          avionics_out = to_avionics(latest_inst_avionics_, now); break;
        case SOURCE_HARDWARE:
          avionics_out = to_avionics(latest_hw_avionics_, now); break;
        default:
          avionics_out = to_avionics(latest_virt_avionics_, now); break;
      }
      last_published_avionics_ = avionics_out;
      pub_avionics_->publish(avionics_out);
    }

    // --- Panel source — per-switch model, report dominant source ---
    {
      bool any_forced = false;
      for (const auto & [id, ctrl] : switch_controls_) {
        if (ctrl.forced) { any_forced = true; break; }
      }
      if (!any_forced) {
        for (const auto & [id, ctrl] : selector_controls_) {
          if (ctrl.forced) { any_forced = true; break; }
        }
      }
      if (sim_frozen_) {
        panel_source_ = SOURCE_FROZEN;
      } else if (any_forced) {
        panel_source_ = SOURCE_INSTRUCTOR;
      } else if (hw_panel_healthy_) {
        panel_source_ = SOURCE_HARDWARE;
      } else {
        panel_source_ = SOURCE_VIRTUAL;
      }
    }
  }

  // ──────────────────────────────────────────────────────────────────
  // 10 Hz — publish arbitration state
  // ──────────────────────────────────────────────────────────────────
  void publish_arbitration_state()
  {
    auto msg = sim_msgs::msg::ArbitrationState();
    msg.header.stamp = this->now();

    msg.flight_source   = flight_source_;
    msg.engine_source   = engine_source_;
    msg.avionics_source = avionics_source_;
    msg.panel_source    = panel_source_;

    msg.hardware_flight_healthy   = hw_flight_healthy_;
    msg.hardware_engine_healthy   = hw_engine_healthy_;
    msg.hardware_avionics_healthy = hw_avionics_healthy_;
    msg.hardware_panel_healthy    = hw_panel_healthy_;

    // Per-switch force lists for frontend FORCE checkbox rendering
    for (const auto & [id, ctrl] : switch_controls_) {
      if (ctrl.forced) msg.forced_switch_ids.push_back(id);
    }
    for (const auto & [id, ctrl] : selector_controls_) {
      if (ctrl.forced) msg.forced_selector_ids.push_back(id);
    }

    pub_arbitration_->publish(msg);
  }

  // ──────────────────────────────────────────────────────────────────
  // Member variables
  // ──────────────────────────────────────────────────────────────────

  // Parameters
  int hardware_timeout_ms_ = 500;
  int update_rate_hz_ = 50;

  // Channel source selection
  uint8_t flight_source_   = SOURCE_VIRTUAL;
  uint8_t engine_source_   = SOURCE_VIRTUAL;
  uint8_t avionics_source_ = SOURCE_VIRTUAL;
  uint8_t panel_source_    = SOURCE_VIRTUAL;

  // Hardware health per channel
  bool hw_flight_healthy_   = false;
  bool hw_engine_healthy_   = false;
  bool hw_avionics_healthy_ = false;
  bool hw_panel_healthy_    = false;

  // Instructor presence flags (sticky — once instructor publishes, they own the channel)
  // NOTE: panel channel no longer uses sticky flag — per-switch force model instead
  bool has_inst_flight_   = false;
  bool has_inst_engine_   = false;
  bool has_inst_avionics_ = false;

  // Sim frozen state
  bool sim_frozen_ = false;
  rclcpp::Time last_hw_heartbeat_{0, 0, RCL_ROS_TIME};

  // Latest raw inputs per source
  sim_msgs::msg::RawFlightControls   latest_hw_flight_, latest_virt_flight_, latest_inst_flight_;
  sim_msgs::msg::RawEngineControls   latest_hw_engine_, latest_virt_engine_, latest_inst_engine_;
  sim_msgs::msg::RawAvionicsControls latest_hw_avionics_, latest_virt_avionics_, latest_inst_avionics_;

  // Frozen snapshot (captured when entering FROZEN state)
  sim_msgs::msg::FlightControls   frozen_flight_;
  sim_msgs::msg::EngineControls   frozen_engine_;
  sim_msgs::msg::AvionicsControls frozen_avionics_;

  // Last published continuous controls (used to capture frozen snapshot)
  sim_msgs::msg::FlightControls   last_published_flight_;
  sim_msgs::msg::EngineControls   last_published_engine_;
  sim_msgs::msg::AvionicsControls last_published_avionics_;

  // Panel state — per-switch force model (accumulated, on-change only)
  sim_msgs::msg::PanelControls current_panel_;
  sim_msgs::msg::PanelControls last_published_panel_;
  std::unordered_map<std::string, SwitchControlState> switch_controls_;
  std::unordered_map<std::string, SelectorControlState> selector_controls_;
  std::unordered_map<std::string, float> pot_map_;
  std::unordered_map<std::string, int32_t> encoder_abs_map_;

  // Subscribers
  rclcpp::Subscription<sim_msgs::msg::RawFlightControls>::SharedPtr
    sub_hw_flight_, sub_virt_flight_, sub_inst_flight_;
  rclcpp::Subscription<sim_msgs::msg::RawEngineControls>::SharedPtr
    sub_hw_engine_, sub_virt_engine_, sub_inst_engine_;
  rclcpp::Subscription<sim_msgs::msg::RawAvionicsControls>::SharedPtr
    sub_hw_avionics_, sub_virt_avionics_, sub_inst_avionics_;
  rclcpp::Subscription<sim_msgs::msg::PanelControls>::SharedPtr
    sub_hw_panel_, sub_virt_panel_, sub_inst_panel_;
  rclcpp::Subscription<sim_msgs::msg::DeviceHeartbeat>::SharedPtr sub_heartbeat_;
  rclcpp::Subscription<sim_msgs::msg::SimState>::SharedPtr sub_sim_state_;

  // Publishers
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::FlightControls>::SharedPtr pub_flight_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::EngineControls>::SharedPtr pub_engine_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::AvionicsControls>::SharedPtr pub_avionics_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::PanelControls>::SharedPtr pub_panel_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::ArbitrationState>::SharedPtr pub_arbitration_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::SimAlert>::SharedPtr pub_alert_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr heartbeat_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr lifecycle_state_pub_;

  // Timers
  rclcpp::TimerBase::SharedPtr controls_timer_;
  rclcpp::TimerBase::SharedPtr arbitration_timer_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<InputArbitratorNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
