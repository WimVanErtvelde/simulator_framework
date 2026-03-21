#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <lifecycle_msgs/msg/state.hpp>
#include <std_msgs/msg/header.hpp>
#include <std_msgs/msg/string.hpp>
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
#include <sim_msgs/msg/terrain_source.hpp>
#include <sim_msgs/srv/get_terrain_elevation.hpp>

#include <map>
#include <chrono>

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
      "/sim/diagnostics/lifecycle_state", 10);
    flight_model_state_pub_ = this->create_publisher<sim_msgs::msg::FlightModelState>(
      "/sim/flight_model/state", 10);

    terrain_source_pub_ = this->create_publisher<sim_msgs::msg::TerrainSource>(
      "/sim/terrain/source", 10);

    // CIGI HOT response subscription — terrain data from IG
    hat_response_sub_ = this->create_subscription<sim_msgs::msg::HatHotResponse>(
      "/sim/cigi/hat_responses", 10,
      [this](const sim_msgs::msg::HatHotResponse::SharedPtr msg) {
        if (msg->valid && !msg->point_name.empty()) {
          terrain_hot_[msg->point_name] = msg->hot;
          last_cigi_hot_time_ = std::chrono::steady_clock::now();
        }
      });

    // SRTM terrain service client (fallback when CIGI unavailable)
    terrain_client_ = this->create_client<sim_msgs::srv::GetTerrainElevation>(
      "/navaid_sim/get_terrain_elevation");

    // Capabilities topic — transient_local so late joiners get it
    auto caps_qos = rclcpp::QoS(1).transient_local().reliable();
    caps_pub_ = this->create_publisher<sim_msgs::msg::FlightModelCapabilities>(
      "/sim/flight_model/capabilities", caps_qos);

    // Sim state subscription — controls whether flight model steps or freezes
    sim_state_sub_ = this->create_subscription<sim_msgs::msg::SimState>(
      "/sim/state", 10,
      [this](const sim_msgs::msg::SimState::SharedPtr msg) {
        uint8_t prev = sim_state_;
        sim_state_ = msg->state;
        if (prev != sim_state_) {
          RCLCPP_INFO(this->get_logger(), "Sim state: %u → %u", prev, sim_state_);
        }
      });

    // IC subscription — apply with SRTM immediately, then refine with CIGI HOT
    ic_sub_ = this->create_subscription<sim_msgs::msg::InitialConditions>(
      "/sim/initial_conditions", 10,
      [this](const sim_msgs::msg::InitialConditions::SharedPtr msg) {
        if (!adapter_) return;
        RCLCPP_INFO(this->get_logger(), "Applying initial conditions: config=%s",
                    msg->configuration.c_str());

        // Clear stale CIGI HOT data — it's from the old position
        terrain_hot_.clear();
        ic_cigi_refined_ = false;
        ic_srtm_applied_ = false;
        srtm_valid_ = false;

        // Step 1: Apply raw IC immediately so JSBSim has a valid position.
        // This moves the entity to the target lat/lon — IG pages terrain there.
        adapter_->apply_initial_conditions(*msg);

        // Save IC for terrain refinement
        pending_ic_ = std::make_shared<sim_msgs::msg::InitialConditions>(*msg);
        pending_ic_time_ = std::chrono::steady_clock::now();

        // Step 2: Query SRTM for terrain (async) — apply as soon as it arrives
        if (terrain_client_ && terrain_client_->service_is_ready()) {
          auto req = std::make_shared<sim_msgs::srv::GetTerrainElevation::Request>();
          req->latitude_rad = msg->latitude_rad;
          req->longitude_rad = msg->longitude_rad;
          terrain_client_->async_send_request(req,
            [this](rclcpp::Client<sim_msgs::srv::GetTerrainElevation>::SharedFuture future) {
              auto resp = future.get();
              if (resp && resp->valid) {
                srtm_terrain_m_ = resp->elevation_msl_m;
                srtm_valid_ = true;
              }
            });
        }
        // Step 3: CIGI HOT refinement handled in update loop (waits for IG response)
      });

    // Engine commands subscription (write-back from sim_engine_systems)
    engine_commands_sub_ = this->create_subscription<sim_msgs::msg::EngineCommands>(
      "/sim/engines/commands", 10,
      [this](const sim_msgs::msg::EngineCommands::SharedPtr msg) {
        if (adapter_) {
          adapter_->apply_engine_commands(*msg);
        }
      });

    // Failure injection subscription (from sim_failures)
    failure_injection_sub_ = this->create_subscription<sim_msgs::msg::FailureInjection>(
      "/sim/failure/flight_model_commands", 10,
      [this](const sim_msgs::msg::FailureInjection::SharedPtr msg) {
        if (adapter_) {
          adapter_->apply_failure(msg->method, msg->params_json, msg->active);
        }
      });

    // Arbitrated controls subscriptions — store latest, applied each step
    flight_controls_sub_ = this->create_subscription<sim_msgs::msg::FlightControls>(
      "/sim/controls/flight", 10,
      [this](const sim_msgs::msg::FlightControls::SharedPtr msg) {
        latest_flight_controls_ = msg;
      });

    engine_controls_sub_ = this->create_subscription<sim_msgs::msg::EngineControls>(
      "/sim/controls/engine", 10,
      [this](const sim_msgs::msg::EngineControls::SharedPtr msg) {
        latest_engine_controls_ = msg;
      });

    // Create the flight model adapter
    auto fdm_type = this->get_parameter("fdm_type").as_string();
    auto aircraft_id = this->get_parameter("aircraft_id").as_string();
    auto jsbsim_root = this->get_parameter("jsbsim_root_dir").as_string();

    RCLCPP_INFO(this->get_logger(), "Configuring flight model: type=%s, aircraft=%s",
      fdm_type.c_str(), aircraft_id.c_str());

    if (fdm_type == "jsbsim") {
      adapter_ = std::make_unique<flight_model_adapter::JSBSimAdapter>();

      if (jsbsim_root.empty()) {
        jsbsim_root = JSBSIM_ROOT_DIR;
      }
      RCLCPP_INFO(this->get_logger(), "JSBSim root dir: %s", jsbsim_root.c_str());

      if (!adapter_->initialize(aircraft_id, jsbsim_root)) {
        RCLCPP_ERROR(this->get_logger(), "Failed to initialize JSBSim adapter");
        return CallbackReturn::FAILURE;
      }
      RCLCPP_INFO(this->get_logger(), "JSBSim model loaded successfully");
      caps_ = adapter_->get_capabilities();
    } else {
      RCLCPP_ERROR(this->get_logger(), "Unknown flight model type: %s", fdm_type.c_str());
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

    // Publish capabilities once (latched via transient_local QoS)
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

    // Subscribe to writeback topics from system nodes
    elec_writeback_sub_ = this->create_subscription<sim_msgs::msg::ElectricalState>(
      "/sim/writeback/electrical", 10,
      [this](const sim_msgs::msg::ElectricalState::SharedPtr msg) {
        if (adapter_) adapter_->write_back_electrical(*msg);
      });

    fuel_writeback_sub_ = this->create_subscription<sim_msgs::msg::FuelState>(
      "/sim/writeback/fuel", 10,
      [this](const sim_msgs::msg::FuelState::SharedPtr msg) {
        if (adapter_) adapter_->write_back_fuel(*msg);
      });

    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::String();
        msg.data = this->get_name();
        heartbeat_pub_->publish(msg);

        // Publish terrain source at 1Hz
        publish_terrain_source();
      });

    double rate_hz = this->get_parameter("update_rate_hz").as_double();
    int period_ms = static_cast<int>(1000.0 / rate_hz);
    double dt_sec = 1.0 / rate_hz;

    update_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(period_ms),
      [this, dt_sec]() {
        if (!adapter_) return;

        // Only step the flight model when sim is RUNNING.
        // FROZEN: publish current state but don't advance.
        // INIT/READY/RESETTING/SHUTDOWN: also don't advance.
        // No sim_manager yet (sim_state_ == 255): step anyway for standalone testing.
        bool should_step = (sim_state_ == sim_msgs::msg::SimState::STATE_RUNNING)
                        || (sim_state_ == 255);  // no sim_manager connected yet

        // Apply arbitrated controls to JSBSim properties before stepping
        if (latest_flight_controls_) {
          auto & fc = *latest_flight_controls_;
          adapter_->set_property("fcs/aileron-cmd-norm",       fc.aileron);
          adapter_->set_property("fcs/elevator-cmd-norm",      fc.elevator);
          adapter_->set_property("fcs/rudder-cmd-norm",        fc.rudder);
          adapter_->set_property("fcs/aileron-trim-cmd-norm",  fc.trim_aileron);
          adapter_->set_property("fcs/pitch-trim-cmd-norm",    fc.trim_elevator);
          adapter_->set_property("fcs/yaw-trim-cmd-norm",      fc.trim_rudder);
          adapter_->set_property("fcs/left-brake-cmd-norm",    fc.brake_left);
          adapter_->set_property("fcs/right-brake-cmd-norm",   fc.brake_right);
          adapter_->set_property("fcs/flap-cmd-norm",          fc.flaps);
          adapter_->set_property("gear/gear-cmd-norm",         fc.gear_down ? 1.0 : 0.0);
          // spoilers: fcs/spoiler-cmd-norm — not in C172P model, skipped
          // speed_brake: fcs/speedbrake-cmd-norm — not in C172P model, skipped
          // parking_brake: no JSBSim property — simulated via left+right brake lock
          // rotor_brake: helicopter only, skipped
        }
        if (latest_engine_controls_) {
          auto & ec = *latest_engine_controls_;
          for (size_t i = 0; i < ec.throttle.size() && i < 4; ++i) {
            adapter_->set_property("fcs/throttle-cmd-norm[" + std::to_string(i) + "]",
                                   ec.throttle[i]);
          }
          for (size_t i = 0; i < ec.mixture.size() && i < 4; ++i) {
            adapter_->set_property("fcs/mixture-cmd-norm[" + std::to_string(i) + "]",
                                   ec.mixture[i]);
          }
        }

        if (should_step) {
          if (!adapter_->step(dt_sec)) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(),
              5000, "Flight model step() returned false");
          }
        }

        // Update JSBSim terrain elevation from HOT data when near ground
        update_terrain_elevation();

        // IC terrain refinement: SRTM fast, CIGI HOT overrides
        if (pending_ic_) {
          auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - pending_ic_time_).count();

          // Apply SRTM as soon as it's ready (typically ~50ms)
          if (!ic_srtm_applied_ && srtm_valid_) {
            RCLCPP_INFO(this->get_logger(),
              "IC terrain from SRTM: %.1f m MSL (after %ldms)", srtm_terrain_m_, age_ms);
            apply_ic_with_terrain(*pending_ic_, srtm_terrain_m_);
            ic_srtm_applied_ = true;
          }

          // CIGI HOT overrides SRTM when available (>200ms for IG to respond)
          if (!ic_cigi_refined_ && !terrain_hot_.empty() && age_ms >= 200) {
            double sum = 0.0;
            for (auto & [name, hot] : terrain_hot_) sum += hot;
            double terrain_elev_m = sum / terrain_hot_.size();
            RCLCPP_INFO(this->get_logger(),
              "IC terrain refined from CIGI HOT: %.1f m MSL (after %ldms)",
              terrain_elev_m, age_ms);
            apply_ic_with_terrain(*pending_ic_, terrain_elev_m);
            ic_cigi_refined_ = true;
            pending_ic_.reset();  // done — CIGI is the best source
          }

          // Timeout at 2s — stop waiting for CIGI
          if (!ic_cigi_refined_ && age_ms > 2000) {
            if (ic_srtm_applied_) {
              RCLCPP_INFO(this->get_logger(), "IC complete — SRTM terrain (no CIGI)");
            } else {
              RCLCPP_WARN(this->get_logger(), "IC timeout — no terrain data available");
            }
            pending_ic_.reset();
          }
        }

        auto state = adapter_->get_state();
        state.is_frozen = (sim_state_ == sim_msgs::msg::SimState::STATE_FROZEN);
        // Legacy bool caps for backward compat — true when FDM_NATIVE
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
    terrain_client_.reset();
    adapter_.reset();
    RCLCPP_INFO(this->get_logger(), "flight_model_adapter cleaned up");
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

  void apply_ic_with_terrain(const sim_msgs::msg::InitialConditions & ic, double terrain_elev_m)
  {
    if (!adapter_) return;

    // Set JSBSim terrain elevation FIRST — force-on-ground needs to know where the ground is
    double terrain_ft = terrain_elev_m * 3.28084;
    adapter_->set_property("position/terrain-elevation-asl-ft", terrain_ft);

    auto modified_ic = ic;
    // For on-ground starts, set altitude to terrain. JSBSim's force-on-ground
    // places the gear on the terrain surface and sets CG height from gear geometry.
    if (modified_ic.altitude_msl_m < terrain_elev_m + 50.0) {
      RCLCPP_INFO(this->get_logger(), "IC terrain: %.1f m MSL (%.0f ft) — on-ground start",
                  terrain_elev_m, terrain_ft);
      modified_ic.altitude_msl_m = terrain_elev_m;
    }
    adapter_->apply_initial_conditions(modified_ic);
  }

  void publish_terrain_source()
  {
    auto msg = sim_msgs::msg::TerrainSource();
    auto now = std::chrono::steady_clock::now();
    auto cigi_age = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - last_cigi_hot_time_).count();

    if (!terrain_hot_.empty() && cigi_age < 2000) {
      msg.source = sim_msgs::msg::TerrainSource::SOURCE_CIGI;
      msg.description = "CIGI HOT (" + std::to_string(terrain_hot_.size()) + " points)";
      terrain_source_ = sim_msgs::msg::TerrainSource::SOURCE_CIGI;
    } else if (terrain_client_ && terrain_client_->service_is_ready()) {
      msg.source = sim_msgs::msg::TerrainSource::SOURCE_SRTM;
      msg.description = "SRTM fallback";
      terrain_source_ = sim_msgs::msg::TerrainSource::SOURCE_SRTM;
    } else {
      msg.source = sim_msgs::msg::TerrainSource::SOURCE_MSL;
      msg.description = "0ft MSL fallback";
      terrain_source_ = sim_msgs::msg::TerrainSource::SOURCE_MSL;
    }
    terrain_source_pub_->publish(msg);
  }

  void update_terrain_elevation()
  {
    if (!adapter_ || terrain_hot_.empty()) return;
    auto state = adapter_->get_state();

    // Use average HOT from all gear points as terrain elevation
    double sum = 0.0;
    for (auto & [name, hot] : terrain_hot_) sum += hot;
    double terrain_m = sum / terrain_hot_.size();
    double terrain_ft = terrain_m * 3.28084;

    // Guard: never set terrain above the aircraft (would force negative AGL → JSBSim crash)
    if (terrain_m > state.altitude_msl_m + 0.5) {
      RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
        "HOT terrain (%.1fm) above aircraft (%.1fm) — skipping update",
        terrain_m, state.altitude_msl_m);
      return;
    }

    adapter_->set_property("position/terrain-elevation-asl-ft", terrain_ft);
  }

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr heartbeat_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr lifecycle_state_pub_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::FlightModelState>::SharedPtr flight_model_state_pub_;
  rclcpp::Publisher<sim_msgs::msg::FlightModelCapabilities>::SharedPtr caps_pub_;
  rclcpp::Subscription<sim_msgs::msg::SimState>::SharedPtr sim_state_sub_;
  rclcpp::Subscription<sim_msgs::msg::InitialConditions>::SharedPtr ic_sub_;
  rclcpp::Subscription<sim_msgs::msg::EngineCommands>::SharedPtr engine_commands_sub_;
  rclcpp::Subscription<sim_msgs::msg::FailureInjection>::SharedPtr failure_injection_sub_;
  rclcpp::Subscription<sim_msgs::msg::FlightControls>::SharedPtr flight_controls_sub_;
  rclcpp::Subscription<sim_msgs::msg::EngineControls>::SharedPtr engine_controls_sub_;
  rclcpp::Subscription<sim_msgs::msg::ElectricalState>::SharedPtr elec_writeback_sub_;
  rclcpp::Subscription<sim_msgs::msg::FuelState>::SharedPtr fuel_writeback_sub_;
  sim_msgs::msg::FlightControls::SharedPtr latest_flight_controls_;
  sim_msgs::msg::EngineControls::SharedPtr latest_engine_controls_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;
  rclcpp::TimerBase::SharedPtr update_timer_;
  std::unique_ptr<flight_model_adapter::IFlightModelAdapter> adapter_;
  flight_model_adapter::FlightModelCapabilities caps_;
  rclcpp::Subscription<sim_msgs::msg::HatHotResponse>::SharedPtr hat_response_sub_;
  rclcpp::Publisher<sim_msgs::msg::TerrainSource>::SharedPtr terrain_source_pub_;
  rclcpp::Client<sim_msgs::srv::GetTerrainElevation>::SharedPtr terrain_client_;
  std::map<std::string, double> terrain_hot_;  // point_name → terrain MSL (m)
  std::chrono::steady_clock::time_point last_cigi_hot_time_{};
  uint8_t terrain_source_{sim_msgs::msg::TerrainSource::SOURCE_UNKNOWN};
  uint8_t sim_state_{255};  // 255 = no sim_manager connected yet

  // IC terrain pipeline: 0 MSL → CIGI HOT (preferred) or SRTM (fallback)
  std::shared_ptr<sim_msgs::msg::InitialConditions> pending_ic_;
  std::chrono::steady_clock::time_point pending_ic_time_{};
  bool ic_cigi_refined_ = false;
  bool ic_srtm_applied_ = false;
  double srtm_terrain_m_ = 0.0;
  bool srtm_valid_ = false;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<FlightModelAdapterNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
