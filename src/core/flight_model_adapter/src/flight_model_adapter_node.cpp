#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <lifecycle_msgs/msg/state.hpp>
#include <std_msgs/msg/header.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/bool.hpp>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/initial_conditions.hpp>
#include <sim_msgs/msg/sim_state.hpp>
#include <sim_msgs/msg/engine_commands.hpp>
#include <sim_msgs/msg/failure_injection.hpp>
#include <sim_msgs/msg/flight_model_capabilities.hpp>
#include <sim_msgs/msg/electrical_state.hpp>
#include <sim_msgs/msg/fuel_state.hpp>
#include <sim_msgs/msg/flight_controls.hpp>
#include <sim_msgs/msg/engine_controls.hpp>
#include <sim_msgs/msg/hat_hot_response.hpp>
#include <sim_msgs/msg/payload_command.hpp>
#include <sim_msgs/msg/terrain_source.hpp>
#include <sim_msgs/msg/sim_alert.hpp>

#include <yaml-cpp/yaml.h>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include <map>
#include <chrono>
#include <cmath>

#include "flight_model_adapter/IFlightModelAdapter.hpp"
#include "flight_model_adapter/JSBSimAdapter.hpp"

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class FlightModelAdapterNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  FlightModelAdapterNode()
  : LifecycleNode("flight_model_adapter", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    this->declare_parameter<std::string>("aircraft_id", "c172");
    this->declare_parameter<std::string>("fdm_type", "jsbsim");
    this->declare_parameter<double>("update_rate_hz", 50.0);
    this->declare_parameter<std::string>("jsbsim_root_dir", "");

    auto_start_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      [this]() {
        auto_start_timer_->cancel();
        auto_start_timer_.reset();
        auto state_after_configure = this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE);
        if (state_after_configure.id() !=
            lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE) {
          RCLCPP_ERROR(this->get_logger(),
            "Auto-start: configure failed (state=%s) — node stays unconfigured",
            state_after_configure.label().c_str());
          return;
        }
        this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE);
      });
    RCLCPP_INFO(this->get_logger(), "flight_model_adapter constructed (unconfigured)");
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    // Publishers
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle", 10);
    flight_model_state_pub_ = this->create_publisher<sim_msgs::msg::FlightModelState>(
      "/aircraft/fdm/state", 10);
    terrain_source_pub_ = this->create_publisher<sim_msgs::msg::TerrainSource>(
      "/sim/terrain/source", 10);
    terrain_ready_pub_ = this->create_publisher<std_msgs::msg::Bool>(
      "/sim/terrain/ready", 10);
    alert_pub_ = this->create_publisher<sim_msgs::msg::SimAlert>(
      "/sim/alerts", 10);

    // CIGI HOT response subscription — terrain data from IG (already filtered by cigi_bridge)
    hat_response_sub_ = this->create_subscription<sim_msgs::msg::HatHotResponse>(
      "/sim/cigi/hat_responses", 10,
      [this](const sim_msgs::msg::HatHotResponse::SharedPtr msg) {
        if (!msg->valid || msg->point_name.empty()) return;

        terrain_hot_[msg->point_name] = msg->hot_m;
        last_hot_time_ = std::chrono::steady_clock::now();
        has_cigi_ = true;

        // If we have a pending IC waiting for terrain, check if we can complete it
        // No position check needed — cigi_bridge IG status gate ensures only valid HOT
        if (pending_ic_ && !terrain_hot_.empty()) {
          double terrain_m = average_hot();
          RCLCPP_INFO(this->get_logger(),
            "[IC] terrain from CIGI HOT: %.1fm MSL (point=%s)",
            terrain_m, msg->point_name.c_str());
          apply_ic_with_terrain(*pending_ic_, terrain_m);
          pending_ic_.reset();
          publish_terrain_ready(true);
        }
      });

    // Capabilities topic — transient_local so late joiners get it
    auto caps_qos = rclcpp::QoS(1).transient_local().reliable();
    caps_pub_ = this->create_publisher<sim_msgs::msg::FlightModelCapabilities>(
      "/aircraft/fdm/capabilities", caps_qos);

    // Sim state subscription
    sim_state_sub_ = this->create_subscription<sim_msgs::msg::SimState>(
      "/sim/state", 10,
      [this](const sim_msgs::msg::SimState::SharedPtr msg) {
        uint8_t prev = sim_state_;
        sim_state_ = msg->state;
        freeze_position_ = msg->freeze_position;
        if (prev != sim_state_) {
          RCLCPP_INFO(this->get_logger(), "Sim state: %u → %u", prev, sim_state_);
        }
      });

    // IC subscription — apply once, then wait for HOT to refine altitude
    ic_sub_ = this->create_subscription<sim_msgs::msg::InitialConditions>(
      "/sim/initial_conditions", 10,
      [this](const sim_msgs::msg::InitialConditions::SharedPtr msg) {
        if (!adapter_) return;

        RCLCPP_INFO(this->get_logger(),
          "[IC] received: lat=%.5f° lon=%.5f° alt=%.1fm hdg=%.1f° config=%s",
          msg->latitude_deg,
          msg->longitude_deg,
          msg->altitude_msl_m,
          msg->heading_rad * 180.0 / M_PI,
          msg->configuration.c_str());

        // Clear stale HOT data from previous position
        terrain_hot_.clear();
        publish_terrain_ready(false);

        // Apply IC to JSBSim — moves entity to new lat/lon.
        // Altitude may be approximate; terrain refinement follows via HOT.
        adapter_->apply_initial_conditions(*msg);

        // Save IC for terrain refinement when first HOT response arrives
        pending_ic_ = std::make_shared<sim_msgs::msg::InitialConditions>(*msg);
        pending_ic_time_ = std::chrono::steady_clock::now();
      });

    // Engine commands subscription (write-back from sim_engine_systems)
    engine_commands_sub_ = this->create_subscription<sim_msgs::msg::EngineCommands>(
      "/aircraft/engines/commands", 10,
      [this](const sim_msgs::msg::EngineCommands::SharedPtr msg) {
        if (adapter_) adapter_->apply_engine_commands(*msg);
      });

    // Failure injection subscription (from sim_failures)
    failure_injection_sub_ = this->create_subscription<sim_msgs::msg::FailureInjection>(
      "/sim/failures/route/flight_model", 10,
      [this](const sim_msgs::msg::FailureInjection::SharedPtr msg) {
        if (adapter_) adapter_->apply_failure(msg->method, msg->params_json, msg->active);
      });

    // Arbitrated controls subscriptions
    flight_controls_sub_ = this->create_subscription<sim_msgs::msg::FlightControls>(
      "/aircraft/controls/flight", 10,
      [this](const sim_msgs::msg::FlightControls::SharedPtr msg) {
        latest_flight_controls_ = msg;
      });

    engine_controls_sub_ = this->create_subscription<sim_msgs::msg::EngineControls>(
      "/aircraft/controls/engine", 10,
      [this](const sim_msgs::msg::EngineControls::SharedPtr msg) {
        latest_engine_controls_ = msg;
      });

    // Create the flight model adapter
    auto fdm_type = this->get_parameter("fdm_type").as_string();
    auto aircraft_id = this->get_parameter("aircraft_id").as_string();
    auto jsbsim_root = this->get_parameter("jsbsim_root_dir").as_string();

    RCLCPP_INFO(this->get_logger(), "Configuring flight model: type=%s, aircraft=%s",
      fdm_type.c_str(), aircraft_id.c_str());

    // Load aircraft config.yaml (model name, default IC, gear height)
    std::string jsbsim_model_name;
    double max_engine_rpm = 0.0;
    sim_msgs::msg::InitialConditions default_ic;
    std::string pkg_dir;
    try {
      pkg_dir = ament_index_cpp::get_package_share_directory("aircraft_" + aircraft_id);
      auto config_path = pkg_dir + "/config/config.yaml";
      YAML::Node config = YAML::LoadFile(config_path);
      if (config["jsbsim_model_name"]) {
        jsbsim_model_name = config["jsbsim_model_name"].as<std::string>();
      }
      if (config["gear_ground_height_m"]) {
        gear_cg_height_m_ = config["gear_ground_height_m"].as<double>();
        RCLCPP_INFO(this->get_logger(), "Gear ground height from config: %.2f m", gear_cg_height_m_);
      }
      if (config["initial_conditions"]) {
        auto ic = config["initial_conditions"];
        default_ic.latitude_deg = ic["latitude_deg"].as<double>(0.0);
        default_ic.longitude_deg = ic["longitude_deg"].as<double>(0.0);
        default_ic.altitude_msl_m = ic["altitude_msl_m"].as<double>(0.0);
        default_ic.heading_rad = ic["heading_rad"].as<double>(0.0);
        default_ic.airspeed_ms = ic["airspeed_ms"].as<double>(0.0);
      }
    } catch (const std::exception & e) {
      RCLCPP_WARN(this->get_logger(), "Could not load aircraft config: %s — using defaults",
        e.what());
    }

    // Load engine rpm_max from engine.yaml (single source of truth)
    if (!pkg_dir.empty()) {
      try {
        auto engine_path = pkg_dir + "/config/engine.yaml";
        YAML::Node eng_cfg = YAML::LoadFile(engine_path);
        if (eng_cfg["engines"] && eng_cfg["engines"].IsSequence() && eng_cfg["engines"].size() > 0) {
          max_engine_rpm = eng_cfg["engines"][0]["rpm_max"].as<double>(0.0);
        }
      } catch (const std::exception & e) {
        RCLCPP_DEBUG(this->get_logger(), "No engine.yaml for %s — using default max RPM", aircraft_id.c_str());
      }
    }

    if (fdm_type == "jsbsim") {
      adapter_ = std::make_unique<flight_model_adapter::JSBSimAdapter>();
      auto * jsbsim = static_cast<flight_model_adapter::JSBSimAdapter*>(adapter_.get());
      jsbsim->set_error_logger(
        [this](const std::string & msg) { RCLCPP_ERROR(this->get_logger(), "%s", msg.c_str()); });
      if (max_engine_rpm > 0.0) {
        jsbsim->set_max_engine_rpm(max_engine_rpm);
        RCLCPP_INFO(this->get_logger(), "Max engine RPM from config: %.0f", max_engine_rpm);
      }
      if (jsbsim_root.empty()) {
        jsbsim_root = JSBSIM_ROOT_DIR;
      }
      RCLCPP_INFO(this->get_logger(), "JSBSim root dir: %s", jsbsim_root.c_str());
      if (!adapter_->initialize(aircraft_id, jsbsim_root, jsbsim_model_name, default_ic)) {
        RCLCPP_ERROR(this->get_logger(), "Failed to initialize JSBSim adapter");
        auto alert = sim_msgs::msg::SimAlert();
        alert.severity = sim_msgs::msg::SimAlert::SEVERITY_CRITICAL;
        alert.source = "flight_model_adapter";
        alert.message = "Failed to initialize JSBSim adapter for " + aircraft_id;
        alert_pub_->publish(alert);
        return CallbackReturn::FAILURE;
      }
      RCLCPP_INFO(this->get_logger(), "JSBSim model loaded successfully");
      caps_ = adapter_->get_capabilities();
    } else {
      RCLCPP_ERROR(this->get_logger(), "Unknown flight model type: %s", fdm_type.c_str());
      auto alert = sim_msgs::msg::SimAlert();
      alert.severity = sim_msgs::msg::SimAlert::SEVERITY_CRITICAL;
      alert.source = "flight_model_adapter";
      alert.message = "Unknown flight model type: " + fdm_type;
      alert_pub_->publish(alert);
      return CallbackReturn::FAILURE;
    }

    RCLCPP_INFO(this->get_logger(),
      "flight_model_adapter configured — type: %s, aircraft: %s",
      fdm_type.c_str(), aircraft_id.c_str());
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    flight_model_state_pub_->on_activate();

    // Publish capabilities once (latched)
    {
      auto cap_to_uint8 = [](flight_model_adapter::CapabilityMode m) -> uint8_t {
        switch (m) {
          case flight_model_adapter::CapabilityMode::FDM_NATIVE:          return 0;
          case flight_model_adapter::CapabilityMode::EXTERNAL_COUPLED:    return 1;
          case flight_model_adapter::CapabilityMode::EXTERNAL_DECOUPLED:  return 2;
          default: return 0;
        }
      };
      auto msg = sim_msgs::msg::FlightModelCapabilities();
      msg.header.stamp = this->now();
      msg.electrical         = cap_to_uint8(caps_.electrical);
      msg.hydraulic          = cap_to_uint8(caps_.hydraulic);
      msg.gear_retract       = cap_to_uint8(caps_.gear_retract);
      msg.fuel_quantities    = cap_to_uint8(caps_.fuel_quantities);
      msg.fuel_pump_pressure = cap_to_uint8(caps_.fuel_pump_pressure);
      msg.fuel_crossfeed     = cap_to_uint8(caps_.fuel_crossfeed);
      msg.prop_governor      = cap_to_uint8(caps_.prop_governor);
      msg.beta_range         = cap_to_uint8(caps_.beta_range);
      msg.reverse_thrust     = cap_to_uint8(caps_.reverse_thrust);
      msg.fadec              = cap_to_uint8(caps_.fadec);
      msg.autothrust         = cap_to_uint8(caps_.autothrust);
      msg.starter            = cap_to_uint8(caps_.starter);
      msg.engine_fire        = cap_to_uint8(caps_.engine_fire);
      msg.pneumatic          = cap_to_uint8(caps_.pneumatic);
      msg.bleed_air          = cap_to_uint8(caps_.bleed_air);
      msg.apu                = cap_to_uint8(caps_.apu);
      msg.fuel_control_unit  = cap_to_uint8(caps_.fuel_control_unit);
      msg.ignition_sequence  = cap_to_uint8(caps_.ignition_sequence);
      caps_pub_->publish(msg);
      RCLCPP_INFO(this->get_logger(), "Published flight model capabilities");
    }

    // Writeback subscriptions
    elec_writeback_sub_ = this->create_subscription<sim_msgs::msg::ElectricalState>(
      "/aircraft/writeback/electrical", 10,
      [this](const sim_msgs::msg::ElectricalState::SharedPtr msg) {
        if (adapter_) adapter_->write_back_electrical(*msg);
      });

    fuel_writeback_sub_ = this->create_subscription<sim_msgs::msg::FuelState>(
      "/aircraft/writeback/fuel", 10,
      [this](const sim_msgs::msg::FuelState::SharedPtr msg) {
        if (adapter_) adapter_->write_back_fuel(*msg);
      });

    payload_command_sub_ = this->create_subscription<sim_msgs::msg::PayloadCommand>(
      "/aircraft/payload/command", 10,
      [this](const sim_msgs::msg::PayloadCommand::SharedPtr msg) {
        if (adapter_) adapter_->apply_payload_command(*msg);
      });

    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::String();
        msg.data = this->get_name();
        heartbeat_pub_->publish(msg);
        publish_terrain_source();
      });

    double rate_hz = this->get_parameter("update_rate_hz").as_double();
    int period_ms = static_cast<int>(1000.0 / rate_hz);
    double dt_sec = 1.0 / rate_hz;

    update_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(period_ms),
      [this, dt_sec]() {
        if (!adapter_) return;

        // Only step when RUNNING and no IC is pending.
        // pending_ic_ gates stepping to prevent advancing the FDM while
        // waiting for CIGI HOT terrain data to refine the IC altitude.
        bool should_step = (sim_state_ == sim_msgs::msg::SimState::STATE_RUNNING)
                        && !pending_ic_;

        // Safety timeout: if CIGI never responds, clear pending_ic_ so the
        // FDM doesn't stay locked forever (bug #3).
        if (pending_ic_) {
          auto age = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - pending_ic_time_).count();
          if (age > 30.0) {
            RCLCPP_WARN(this->get_logger(),
              "[IC] pending_ic_ timeout (%.1fs) — no CIGI HOT received, clearing", age);
            pending_ic_.reset();
            publish_terrain_ready(true);
          }
        }

        // Apply arbitrated controls
        if (latest_flight_controls_) {
          auto & fc = *latest_flight_controls_;
          adapter_->set_property("fcs/aileron-cmd-norm",       fc.aileron_norm);
          adapter_->set_property("fcs/elevator-cmd-norm",      fc.elevator_norm);
          adapter_->set_property("fcs/rudder-cmd-norm",        fc.rudder_norm);
          adapter_->set_property("fcs/aileron-trim-cmd-norm",  fc.trim_aileron_norm);
          adapter_->set_property("fcs/pitch-trim-cmd-norm",    fc.trim_elevator_norm);
          adapter_->set_property("fcs/yaw-trim-cmd-norm",      fc.trim_rudder_norm);
          adapter_->set_property("fcs/left-brake-cmd-norm",    fc.brake_left_norm);
          adapter_->set_property("fcs/right-brake-cmd-norm",   fc.brake_right_norm);
          adapter_->set_property("fcs/flap-cmd-norm",          fc.flaps_norm);
          adapter_->set_property("gear/gear-cmd-norm",         fc.gear_down ? 1.0 : 0.0);
        }
        if (latest_engine_controls_) {
          auto & ec = *latest_engine_controls_;
          for (size_t i = 0; i < ec.throttle_norm.size() && i < 4; ++i) {
            adapter_->set_property("fcs/throttle-cmd-norm[" + std::to_string(i) + "]",
                                   ec.throttle_norm[i]);
          }
          for (size_t i = 0; i < ec.mixture_norm.size() && i < 4; ++i) {
            adapter_->set_property("fcs/mixture-cmd-norm[" + std::to_string(i) + "]",
                                   ec.mixture_norm[i]);
          }
          // Magnetos: compute magneto_cmd from left/right bools
          // 0=off, 1=left only, 2=right only, 3=both
          int mag = 0;
          bool ml = !ec.magneto_left.empty() && ec.magneto_left[0];
          bool mr = !ec.magneto_right.empty() && ec.magneto_right[0];
          if (ml && mr) mag = 3;
          else if (ml) mag = 1;
          else if (mr) mag = 2;
          adapter_->set_property("propulsion/magneto_cmd", mag);
          // Starter: NOT written here — authority is in engines plugin via EngineCommands
          // (bus voltage gated). See apply_engine_commands().
        }

        // Freeze position: capture on rising edge, write back after step
        if (freeze_position_ && !prev_freeze_position_) {
          frozen_lat_gc_deg_ = adapter_->get_property("position/lat-gc-deg");
          frozen_lon_gc_deg_ = adapter_->get_property("position/long-gc-deg");
          frozen_alt_sl_ft_  = adapter_->get_property("position/h-sl-ft");
          RCLCPP_INFO(this->get_logger(),
            "[FRZ POS] captured: lat_gc=%.8f lon=%.8f alt=%.1fft",
            frozen_lat_gc_deg_, frozen_lon_gc_deg_, frozen_alt_sl_ft_);
        }
        prev_freeze_position_ = freeze_position_;

        if (should_step) {
          if (!adapter_->step(dt_sec)) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(),
              5000, "Flight model step() returned false");
          }
        }

        // Write frozen position back after step — JSBSim computed
        // engines/aero this frame, now slam position back
        if (freeze_position_ && should_step) {
          adapter_->set_property("position/lat-gc-deg", frozen_lat_gc_deg_);
          adapter_->set_property("position/long-gc-deg", frozen_lon_gc_deg_);
          adapter_->set_property("position/h-sl-ft", frozen_alt_sl_ft_);
          adapter_->set_property("velocities/u-fps", 0.0);
          adapter_->set_property("velocities/v-fps", 0.0);
          adapter_->set_property("velocities/w-fps", 0.0);
        }

        auto state = adapter_->get_state();

        // Continuous terrain update from HOT data (non-IC)
        if (!pending_ic_) {
          update_terrain_elevation(state);
        }

        state.is_frozen = (sim_state_ == sim_msgs::msg::SimState::STATE_FROZEN);
        state.cap_models_fuel_quantities    =
          (caps_.fuel_quantities == flight_model_adapter::CapabilityMode::FDM_NATIVE);
        state.cap_models_fuel_pump_pressure =
          (caps_.fuel_pump_pressure == flight_model_adapter::CapabilityMode::FDM_NATIVE);
        state.cap_models_fuel_crossfeed     =
          (caps_.fuel_crossfeed == flight_model_adapter::CapabilityMode::FDM_NATIVE);
        state.header.stamp = this->now();
        flight_model_state_pub_->publish(state);
      });

    RCLCPP_INFO(this->get_logger(), "flight_model_adapter activated (%.0f Hz)", rate_hz);
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    flight_model_state_pub_->on_deactivate();
    heartbeat_timer_.reset();
    update_timer_.reset();
    elec_writeback_sub_.reset();
    fuel_writeback_sub_.reset();
    latest_flight_controls_.reset();
    latest_engine_controls_.reset();
    terrain_hot_.clear();
    pending_ic_.reset();
    RCLCPP_INFO(this->get_logger(), "flight_model_adapter deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    heartbeat_pub_.reset();
    lifecycle_state_pub_.reset();
    flight_model_state_pub_.reset();
    caps_pub_.reset();
    sim_state_sub_.reset();
    ic_sub_.reset();
    engine_commands_sub_.reset();
    failure_injection_sub_.reset();
    flight_controls_sub_.reset();
    engine_controls_sub_.reset();
    elec_writeback_sub_.reset();
    fuel_writeback_sub_.reset();
    hat_response_sub_.reset();
    terrain_source_pub_.reset();
    terrain_ready_pub_.reset();
    adapter_.reset();
    RCLCPP_INFO(this->get_logger(), "flight_model_adapter cleaned up");
    publish_lifecycle_state("unconfigured");
    return CallbackReturn::SUCCESS;
  }

private:
  // ── Helpers ───────────────────────────────────────────────────────────
  void publish_lifecycle_state(const std::string & state)
  {
    if (lifecycle_state_pub_) {
      auto msg = std_msgs::msg::String();
      msg.data = std::string(this->get_name()) + ":" + state;
      lifecycle_state_pub_->publish(msg);
    }
  }

  void publish_terrain_ready(bool ready)
  {
    if (terrain_ready_pub_) {
      auto msg = std_msgs::msg::Bool();
      msg.data = ready;
      terrain_ready_pub_->publish(msg);
    }
  }

  double average_hot() const
  {
    if (terrain_hot_.empty()) return 0.0;
    double sum = 0.0;
    for (const auto & [name, hot] : terrain_hot_) sum += hot;
    return sum / terrain_hot_.size();
  }

  void apply_ic_with_terrain(const sim_msgs::msg::InitialConditions & ic, double terrain_elev_m)
  {
    if (!adapter_) return;
    bool on_ground = (ic.configuration == "ready_for_takeoff" ||
                      ic.configuration == "cold_and_dark" ||
                      ic.altitude_msl_m < terrain_elev_m + 50.0);
    double alt_m = on_ground ? (terrain_elev_m + gear_cg_height_m_) : ic.altitude_msl_m;
    adapter_->refine_terrain_altitude(alt_m, terrain_elev_m);
    RCLCPP_INFO(this->get_logger(),
      "IC terrain refined: terrain=%.1fm, CG=%.1fm, on_ground=%s",
      terrain_elev_m, alt_m, on_ground ? "true" : "false");
  }

  void publish_terrain_source()
  {
    auto msg = sim_msgs::msg::TerrainSource();
    auto now = std::chrono::steady_clock::now();
    auto hot_age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - last_hot_time_).count();

    if (has_cigi_ && hot_age_ms < 2000) {
      msg.source = sim_msgs::msg::TerrainSource::SOURCE_CIGI;
      msg.description = "CIGI HOT (" + std::to_string(terrain_hot_.size()) + " points)";
    } else if (has_cigi_) {
      msg.source = sim_msgs::msg::TerrainSource::SOURCE_CIGI;
      msg.description = "CIGI HOT (stale)";
    } else {
      msg.source = sim_msgs::msg::TerrainSource::SOURCE_MSL;
      msg.description = "No CIGI — 0ft MSL";
    }
    terrain_source_pub_->publish(msg);
  }

  void update_terrain_elevation(const sim_msgs::msg::FlightModelState & state)
  {
    if (!adapter_ || terrain_hot_.empty()) return;

    double terrain_m = average_hot();
    double terrain_ft = terrain_m * 3.28084;

    // Never set terrain above the aircraft
    if (terrain_m > state.altitude_msl_m + 0.5) {
      RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
        "HOT terrain (%.1fm) above aircraft (%.1fm) — skipping",
        terrain_m, state.altitude_msl_m);
      return;
    }

    adapter_->set_property("position/terrain-elevation-asl-ft", terrain_ft);
  }

  // ── ROS2 interfaces ───────────────────────────────────────────────────
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr heartbeat_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr lifecycle_state_pub_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::FlightModelState>::SharedPtr flight_model_state_pub_;
  rclcpp::Publisher<sim_msgs::msg::FlightModelCapabilities>::SharedPtr caps_pub_;
  rclcpp::Publisher<sim_msgs::msg::TerrainSource>::SharedPtr terrain_source_pub_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr terrain_ready_pub_;
  rclcpp::Publisher<sim_msgs::msg::SimAlert>::SharedPtr alert_pub_;

  rclcpp::Subscription<sim_msgs::msg::SimState>::SharedPtr sim_state_sub_;
  rclcpp::Subscription<sim_msgs::msg::InitialConditions>::SharedPtr ic_sub_;
  rclcpp::Subscription<sim_msgs::msg::EngineCommands>::SharedPtr engine_commands_sub_;
  rclcpp::Subscription<sim_msgs::msg::FailureInjection>::SharedPtr failure_injection_sub_;
  rclcpp::Subscription<sim_msgs::msg::FlightControls>::SharedPtr flight_controls_sub_;
  rclcpp::Subscription<sim_msgs::msg::EngineControls>::SharedPtr engine_controls_sub_;
  rclcpp::Subscription<sim_msgs::msg::ElectricalState>::SharedPtr elec_writeback_sub_;
  rclcpp::Subscription<sim_msgs::msg::FuelState>::SharedPtr fuel_writeback_sub_;
  rclcpp::Subscription<sim_msgs::msg::PayloadCommand>::SharedPtr payload_command_sub_;
  rclcpp::Subscription<sim_msgs::msg::HatHotResponse>::SharedPtr hat_response_sub_;

  sim_msgs::msg::FlightControls::SharedPtr latest_flight_controls_;
  sim_msgs::msg::EngineControls::SharedPtr latest_engine_controls_;

  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;
  rclcpp::TimerBase::SharedPtr update_timer_;

  std::unique_ptr<flight_model_adapter::IFlightModelAdapter> adapter_;
  flight_model_adapter::FlightModelCapabilities caps_;
  uint8_t sim_state_{255};
  bool freeze_position_ = false;
  bool prev_freeze_position_ = false;
  double frozen_lat_gc_deg_ = 0.0;
  double frozen_lon_gc_deg_ = 0.0;
  double frozen_alt_sl_ft_  = 0.0;

  // ── Terrain ───────────────────────────────────────────────────────────
  std::map<std::string, double> terrain_hot_;  // point_name → terrain MSL (m)
  std::chrono::steady_clock::time_point last_hot_time_{};
  bool has_cigi_ = false;

  // ── IC terrain refinement ─────────────────────────────────────────────
  // pending_ic_ set on IC receipt, cleared when HOT response refines altitude or on 5s timeout
  std::shared_ptr<sim_msgs::msg::InitialConditions> pending_ic_;
  std::chrono::steady_clock::time_point pending_ic_time_{};
  double gear_cg_height_m_ = 0.5;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<FlightModelAdapterNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
