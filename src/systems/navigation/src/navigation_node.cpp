#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <lifecycle_msgs/msg/state.hpp>
#include <std_msgs/msg/string.hpp>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/nav_signal_table.hpp>
#include <sim_msgs/msg/avionics_controls.hpp>
#include <sim_msgs/msg/navigation_state.hpp>
#include <sim_msgs/msg/failure_state.hpp>
#include <sim_msgs/msg/sim_state.hpp>
#include <sim_msgs/msg/sim_alert.hpp>
#include <std_msgs/msg/float32.hpp>

#include <algorithm>
#include <cmath>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

static constexpr double RAD_TO_DEG = 180.0 / M_PI;
static constexpr double M_TO_NM = 1.0 / 1852.0;
static constexpr double M_TO_FT = 3.28084;
static constexpr double MS_TO_KT = 1.94384;

// Signal strength threshold below which a receiver is considered invalid
static constexpr double SIGNAL_THRESHOLD = 0.1;

class NavigationNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  NavigationNode()
  : LifecycleNode("sim_navigation", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    this->declare_parameter<double>("update_rate_hz", 10.0);

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
    RCLCPP_INFO(this->get_logger(), "sim_navigation constructed (unconfigured)");
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    // Publishers
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle_state", 10);
    alert_pub_ = this->create_publisher<sim_msgs::msg::SimAlert>(
      "/sim/alerts", 10);
    nav_state_pub_ = this->create_publisher<sim_msgs::msg::NavigationState>(
      "/sim/navigation/state", 10);

    // Subscriptions
    flight_model_sub_ = this->create_subscription<sim_msgs::msg::FlightModelState>(
      "/sim/flight_model/state", 10,
      [this](const sim_msgs::msg::FlightModelState::SharedPtr msg) {
        last_flight_model_state_ = *msg;
        flight_model_received_ = true;
      });

    nav_signals_sub_ = this->create_subscription<sim_msgs::msg::NavSignalTable>(
      "/sim/world/nav_signals", 10,
      [this](const sim_msgs::msg::NavSignalTable::SharedPtr msg) {
        last_nav_signals_ = *msg;
        nav_signals_received_ = true;
      });

    avionics_sub_ = this->create_subscription<sim_msgs::msg::AvionicsControls>(
      "/sim/controls/avionics", 10,
      [this](const sim_msgs::msg::AvionicsControls::SharedPtr msg) {
        last_avionics_ = *msg;
      });

    sim_state_sub_ = this->create_subscription<sim_msgs::msg::SimState>(
      "/sim/state", 10,
      [this](const sim_msgs::msg::SimState::SharedPtr msg) {
        auto prev = sim_state_;
        sim_state_ = msg->state;
        if (sim_state_ == sim_msgs::msg::SimState::STATE_RESETTING &&
            prev != sim_msgs::msg::SimState::STATE_RESETTING) {
          dme_hold_valid_ = false;
          dme_hold_distance_nm_ = 0.0f;
          dme_hold_gs_kt_ = 0.0f;
          RCLCPP_INFO(this->get_logger(), "Navigation state reset (DME HOLD cleared)");
        }
      });

    // Magnetic variation from navaid_sim (WMM, 1 Hz)
    mag_var_sub_ = this->create_subscription<std_msgs::msg::Float32>(
      "/sim/world/magnetic_variation_deg", 10,
      [this](const std_msgs::msg::Float32::SharedPtr msg) {
        mag_variation_deg_ = msg->data;
      });

    failure_state_sub_ = this->create_subscription<sim_msgs::msg::FailureState>(
      "/sim/failure_state",
      rclcpp::QoS(10).reliable(),
      [this](const sim_msgs::msg::FailureState::SharedPtr msg) {
        latest_failure_state_ = msg;
      });

    RCLCPP_INFO(this->get_logger(), "sim_navigation configured");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    nav_state_pub_->on_activate();

    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::String();
        msg.data = this->get_name();
        heartbeat_pub_->publish(msg);
      });

    double rate_hz = this->get_parameter("update_rate_hz").as_double();
    int period_ms = static_cast<int>(1000.0 / rate_hz);
    update_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(period_ms),
      [this]() { on_update(); });

    RCLCPP_INFO(this->get_logger(), "sim_navigation activated (%.0f Hz)",
      this->get_parameter("update_rate_hz").as_double());
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    nav_state_pub_->on_deactivate();
    heartbeat_timer_.reset();
    update_timer_.reset();
    RCLCPP_INFO(this->get_logger(), "sim_navigation deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    heartbeat_pub_.reset();
    lifecycle_state_pub_.reset();
    alert_pub_.reset();
    nav_state_pub_.reset();
    flight_model_sub_.reset();
    nav_signals_sub_.reset();
    avionics_sub_.reset();
    sim_state_sub_.reset();
    mag_var_sub_.reset();
    failure_state_sub_.reset();
    latest_failure_state_.reset();
    RCLCPP_INFO(this->get_logger(), "sim_navigation cleaned up");
    publish_lifecycle_state("unconfigured");
    return CallbackReturn::SUCCESS;
  }

private:
  void on_update()
  {
    auto out = sim_msgs::msg::NavigationState();
    out.header.stamp = this->now();

    // ── GPS1 (derived from FDM state) ──────────────────────────────────────
    if (flight_model_received_) {
      out.gps1_valid = true;
      out.gps1_lat_deg = last_flight_model_state_.latitude_deg;
      out.gps1_lon_deg = last_flight_model_state_.longitude_deg;
      out.gps1_alt_ft = static_cast<float>(last_flight_model_state_.altitude_msl_m * M_TO_FT);
      out.gps1_gs_kt = static_cast<float>(last_flight_model_state_.ground_speed_ms * MS_TO_KT);
      out.gps1_track_deg = static_cast<float>(last_flight_model_state_.ground_track_rad * RAD_TO_DEG);
    }

    // ── GPS source selector echo ───────────────────────────────────────────
    out.active_gps_source = last_avionics_.gps_source;

    // ── Transponder (pass through from avionics controls) ──────────────────
    out.transponder_code = last_avionics_.transponder_code;
    out.transponder_mode = last_avionics_.transponder_mode;
    out.transponder_ident_active = false;  // not yet implemented

    // ── Frequency echoes (for IOS display convenience) ─────────────────────
    out.com1_freq_mhz = last_avionics_.com1_freq_mhz;
    out.com2_freq_mhz = last_avionics_.com2_freq_mhz;
    out.com3_freq_mhz = last_avionics_.com3_freq_mhz;
    out.adf1_freq_khz = last_avionics_.adf1_freq_khz;
    out.adf2_freq_khz = last_avionics_.adf2_freq_khz;

    // ── Nav receivers (from navaid_sim signals + avionics OBS) ──────────────
    if (nav_signals_received_) {
      const auto & ns = last_nav_signals_;
      float obs1 = last_avionics_.obs1_deg;
      float obs2 = last_avionics_.obs2_deg;
      double hdg_deg = flight_model_received_ ? last_flight_model_state_.true_heading_rad * RAD_TO_DEG : 0.0;

      // NAV1
      process_nav_receiver(
        ns.nav1_valid, ns.nav1_ident, ns.nav1_type,
        ns.nav1_bearing_rad, ns.nav1_radial_rad,
        ns.nav1_deviation_dots,
        ns.nav1_distance_m, ns.nav1_signal_strength,
        ns.nav1_gs_valid, ns.nav1_gs_deviation_dots,
        obs1,
        out.nav1_valid, out.nav1_ident, out.nav1_type,
        out.nav1_obs_deg, out.nav1_cdi_dots,
        out.nav1_bearing_deg, out.nav1_radial_deg,
        out.nav1_distance_nm, out.nav1_signal_strength,
        out.nav1_to_from,
        out.nav1_gs_valid, out.nav1_gs_dots);

      // NAV2
      process_nav_receiver(
        ns.nav2_valid, ns.nav2_ident, ns.nav2_type,
        ns.nav2_bearing_rad, ns.nav2_radial_rad,
        ns.nav2_deviation_dots,
        ns.nav2_distance_m, ns.nav2_signal_strength,
        ns.nav2_gs_valid, ns.nav2_gs_deviation_dots,
        obs2,
        out.nav2_valid, out.nav2_ident, out.nav2_type,
        out.nav2_obs_deg, out.nav2_cdi_dots,
        out.nav2_bearing_deg, out.nav2_radial_deg,
        out.nav2_distance_nm, out.nav2_signal_strength,
        out.nav2_to_from,
        out.nav2_gs_valid, out.nav2_gs_dots);

      // ADF1 — convert absolute bearing to relative bearing
      bool adf1_ok = ns.adf_valid && ns.adf_signal_strength >= SIGNAL_THRESHOLD;
      out.adf1_valid = adf1_ok;
      if (adf1_ok) {
        out.adf1_ident = ns.adf_ident;
        double adf_abs_deg = ns.adf_bearing_rad * RAD_TO_DEG;
        out.adf1_relative_bearing_deg = static_cast<float>(normalize_180(adf_abs_deg - hdg_deg));
        out.adf1_signal = static_cast<float>(ns.adf_signal_strength);
      }

      // Markers
      out.marker_outer = ns.marker_outer;
      out.marker_middle = ns.marker_middle;
      out.marker_inner = ns.marker_inner;

      // DME — source selected by dme_source: NAV1(0), HOLD(1), NAV2(2)
      uint8_t dme_src = last_avionics_.dme_source;
      out.dme_source = dme_src;

      if (dme_src == sim_msgs::msg::AvionicsControls::DME_SOURCE_HOLD) {
        // HOLD: keep last valid DME reading (don't update)
        out.dme_valid = dme_hold_valid_;
        out.dme_distance_nm = dme_hold_distance_nm_;
        out.dme_gs_kt = dme_hold_gs_kt_;
      } else {
        // NAV1 or NAV2: DME is paired with the selected NAV receiver
        bool use_nav2 = (dme_src == sim_msgs::msg::AvionicsControls::DME_SOURCE_NAV2);
        double dme_dist_m = use_nav2 ? ns.nav2_distance_m : ns.nav1_distance_m;
        bool nav_valid = use_nav2 ? ns.nav2_valid : ns.nav1_valid;
        double nav_strength = use_nav2 ? ns.nav2_signal_strength : ns.nav1_signal_strength;
        // DME is valid if the paired NAV has a VORDME or ILS with DME
        uint8_t nav_type = use_nav2 ? ns.nav2_type : ns.nav1_type;
        bool has_dme = (nav_type == 2 || nav_type == 3);  // VORDME or ILS
        bool dme_ok = nav_valid && has_dme && nav_strength >= SIGNAL_THRESHOLD && dme_dist_m > 0.0;

        out.dme_valid = dme_ok;
        if (dme_ok) {
          out.dme_distance_nm = static_cast<float>(dme_dist_m * M_TO_NM);
          out.dme_gs_kt = static_cast<float>(ns.dme_ground_speed_kts);
        }

        // Update hold values whenever we have a valid reading
        if (dme_ok) {
          dme_hold_valid_ = true;
          dme_hold_distance_nm_ = out.dme_distance_nm;
          dme_hold_gs_kt_ = out.dme_gs_kt;
        }
      }
    }

    // ── Failure gating — zero outputs for failed receivers ─────────────
    auto is_failed = [&](const std::string & id) -> bool {
      if (!latest_failure_state_) return false;
      const auto & f = latest_failure_state_->failed_nav_receivers;
      return std::find(f.begin(), f.end(), id) != f.end();
    };

    if (is_failed("NAV1")) {
      out.nav1_valid = false;
      out.nav1_ident = "";
      out.nav1_cdi_dots = 0.0f;
      out.nav1_bearing_deg = 0.0f;
      out.nav1_radial_deg = 0.0f;
      out.nav1_distance_nm = 0.0f;
      out.nav1_signal_strength = 0.0f;
      out.nav1_to_from = 0;
      out.nav1_gs_valid = false;
      out.nav1_gs_dots = 0.0f;
    }

    if (is_failed("NAV2")) {
      out.nav2_valid = false;
      out.nav2_ident = "";
      out.nav2_cdi_dots = 0.0f;
      out.nav2_bearing_deg = 0.0f;
      out.nav2_radial_deg = 0.0f;
      out.nav2_distance_nm = 0.0f;
      out.nav2_signal_strength = 0.0f;
      out.nav2_to_from = 0;
      out.nav2_gs_valid = false;
      out.nav2_gs_dots = 0.0f;
    }

    if (is_failed("ADF")) {
      out.adf1_valid = false;
      out.adf1_ident = "";
      out.adf1_relative_bearing_deg = 0.0f;
      out.adf1_signal = 0.0f;
    }

    if (is_failed("GPS1")) {
      out.gps1_valid = false;
      out.gps1_lat_deg = 0.0;
      out.gps1_lon_deg = 0.0;
      out.gps1_alt_ft = 0.0f;
      out.gps1_gs_kt = 0.0f;
      out.gps1_track_deg = 0.0f;
    }

    if (is_failed("GPS2")) {
      out.gps2_valid = false;
      out.gps2_lat_deg = 0.0;
      out.gps2_lon_deg = 0.0;
      out.gps2_alt_ft = 0.0f;
      out.gps2_gs_kt = 0.0f;
      out.gps2_track_deg = 0.0f;
    }

    // DME: also invalidate if the paired NAV receiver has failed
    if ((out.dme_source == 0 && is_failed("NAV1")) ||
        (out.dme_source == 2 && is_failed("NAV2"))) {
      out.dme_valid = false;
      out.dme_distance_nm = 0.0f;
      out.dme_gs_kt = 0.0f;
    }

    // Compass heading: true heading - WMM declination
    if (flight_model_received_) {
      float true_hdg = static_cast<float>(last_flight_model_state_.true_heading_rad);
      float var_rad = mag_variation_deg_ * static_cast<float>(M_PI / 180.0);
      float mag_hdg = true_hdg - var_rad;
      while (mag_hdg < 0.0f) mag_hdg += static_cast<float>(2.0 * M_PI);
      while (mag_hdg >= static_cast<float>(2.0 * M_PI)) mag_hdg -= static_cast<float>(2.0 * M_PI);
      out.magnetic_heading_rad = mag_hdg;
      out.magnetic_variation_deg = mag_variation_deg_;
    }

    nav_state_pub_->publish(out);
  }

  // Process one NAV receiver (VOR/ILS/LOC)
  static void process_nav_receiver(
    bool sig_valid, const std::string & sig_ident, uint8_t sig_type,
    double sig_bearing_rad, double sig_radial_rad,
    double sig_deviation_dots,
    double sig_distance_m, double sig_strength,
    bool sig_gs_valid, double sig_gs_dots,
    float obs_deg,
    // outputs (float32 — msg field references)
    bool & valid, std::string & ident, uint8_t & type,
    float & obs_out, float & cdi_dots,
    float & bearing_deg, float & radial_deg,
    float & distance_nm, float & signal_strength,
    uint8_t & to_from,
    bool & gs_valid, float & gs_dots)
  {
    bool ok = sig_valid && sig_strength >= SIGNAL_THRESHOLD;
    valid = ok;
    obs_out = obs_deg;

    if (!ok) {
      to_from = 0;  // OFF
      return;
    }

    ident = sig_ident;
    type = sig_type;
    bearing_deg = static_cast<float>(sig_bearing_rad * RAD_TO_DEG);
    radial_deg = static_cast<float>(sig_radial_rad * RAD_TO_DEG);
    distance_nm = static_cast<float>(sig_distance_m * M_TO_NM);
    signal_strength = static_cast<float>(sig_strength);

    bool is_loc = (sig_type == 3 || sig_type == 4);  // ILS or LOC-only

    if (is_loc) {
      // LOC/ILS: CDI tracks the localizer course, OBS is ignored
      // navaid_sim already computes deviation in dots from the front course
      cdi_dots = static_cast<float>(std::clamp(sig_deviation_dots, -2.5, 2.5));
      to_from = 0;  // no TO/FROM flag on LOC/ILS
    } else {
      // VOR: CDI deflection relative to selected OBS course
      double radial_from_deg = sig_radial_rad * RAD_TO_DEG;
      cdi_dots = static_cast<float>(compute_vor_cdi(radial_from_deg, obs_deg));
      to_from = compute_to_from(radial_from_deg, obs_deg);
    }

    // Glideslope
    gs_valid = sig_gs_valid && is_loc;
    if (gs_valid) {
      gs_dots = static_cast<float>(std::clamp(sig_gs_dots, -2.5, 2.5));
    }
  }

  // VOR CDI: angular difference between selected course (OBS) and actual radial
  // Full scale = +/-10 deg = +/-2.5 dots -> 1 dot = 4 deg
  static double compute_vor_cdi(double radial_from_deg, double obs_deg)
  {
    double diff = normalize_180(obs_deg - radial_from_deg);
    double dots = diff / 4.0;
    return std::clamp(dots, -2.5, 2.5);
  }

  // TO/FROM flag based on OBS course vs current radial
  // Within +/-90 deg of OBS -> TO, otherwise -> FROM
  static uint8_t compute_to_from(double radial_from_deg, double obs_deg)
  {
    double diff = normalize_180(obs_deg - radial_from_deg);
    if (std::abs(diff) <= 90.0) {
      return 1;  // TO
    } else {
      return 2;  // FROM
    }
  }

  static double normalize_180(double deg)
  {
    while (deg > 180.0) deg -= 360.0;
    while (deg < -180.0) deg += 360.0;
    return deg;
  }

  void publish_lifecycle_state(const std::string & state)
  {
    if (lifecycle_state_pub_) {
      auto msg = std_msgs::msg::String();
      msg.data = std::string(this->get_name()) + ":" + state;
      lifecycle_state_pub_->publish(msg);
    }
  }

  // Publishers
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr heartbeat_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr lifecycle_state_pub_;
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::NavigationState>::SharedPtr nav_state_pub_;
  rclcpp::Publisher<sim_msgs::msg::SimAlert>::SharedPtr alert_pub_;

  // Subscriptions
  rclcpp::Subscription<sim_msgs::msg::FlightModelState>::SharedPtr flight_model_sub_;
  rclcpp::Subscription<sim_msgs::msg::NavSignalTable>::SharedPtr nav_signals_sub_;
  rclcpp::Subscription<sim_msgs::msg::AvionicsControls>::SharedPtr avionics_sub_;
  rclcpp::Subscription<sim_msgs::msg::SimState>::SharedPtr sim_state_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr mag_var_sub_;
  rclcpp::Subscription<sim_msgs::msg::FailureState>::SharedPtr failure_state_sub_;
  // Timers
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;
  rclcpp::TimerBase::SharedPtr update_timer_;

  // Latest received state
  sim_msgs::msg::FlightModelState last_flight_model_state_;
  sim_msgs::msg::NavSignalTable last_nav_signals_;
  sim_msgs::msg::AvionicsControls last_avionics_;
  bool flight_model_received_{false};
  bool nav_signals_received_{false};

  uint8_t sim_state_{0};
  float mag_variation_deg_{0.0f};  // from navaid_sim WMM (east positive)

  // Latest failure state from sim_failures
  sim_msgs::msg::FailureState::SharedPtr latest_failure_state_;

  // DME HOLD state — frozen when selector is in HOLD position
  bool dme_hold_valid_{false};
  float dme_hold_distance_nm_{0.0f};
  float dme_hold_gs_kt_{0.0f};
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<NavigationNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
