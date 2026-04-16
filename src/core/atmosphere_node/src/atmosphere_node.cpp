#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <lifecycle_msgs/msg/state.hpp>
#include <std_msgs/msg/header.hpp>
#include <std_msgs/msg/string.hpp>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/weather_state.hpp>
#include <sim_msgs/msg/atmosphere_state.hpp>

#include <cmath>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

// ─── ISA Constants (ICAO Doc 7488) ──────────────────────────────
static constexpr double ISA_T0       = 288.15;    // sea-level temperature (K)
static constexpr double ISA_P0       = 101325.0;  // sea-level pressure (Pa)
static constexpr double ISA_RHO0     = 1.225;     // sea-level density (kg/m^3)
static constexpr double ISA_LAPSE    = 0.0065;    // troposphere lapse rate (K/m)
static constexpr double ISA_R        = 287.058;   // specific gas constant dry air (J/(kg·K))
static constexpr double ISA_GAMMA    = 1.4;       // ratio of specific heats
static constexpr double ISA_TROPO_ALT = 11000.0;  // tropopause altitude (m)
static constexpr double ISA_T_TROPO  = 216.65;    // tropopause temperature (K)
static constexpr double ISA_P_TROPO  = 22632.1;   // pressure at tropopause (Pa)
static constexpr double ISA_G        = 9.80665;   // standard gravity (m/s^2)

// Hypsometric constants for altitude-from-pressure/density
// pressure_altitude  = 44330.77 * (1 - (P/P0)^0.190284)
// density_altitude   = 44330.77 * (1 - (rho/rho0)^0.234969)
static constexpr double HYPSO_SCALE  = 44330.77;
static constexpr double HYPSO_P_EXP  = 0.190284;
static constexpr double HYPSO_D_EXP  = 0.234969;

class AtmosphereNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  AtmosphereNode()
  : LifecycleNode("atmosphere_node", rclcpp::NodeOptions().parameter_overrides(
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
    RCLCPP_INFO(this->get_logger(), "atmosphere_node constructed (unconfigured)");
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    // Publishers
    atmo_pub_ = this->create_publisher<sim_msgs::msg::AtmosphereState>(
      "/world/atmosphere", 10);
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle", 10);

    // Subscriptions
    flight_model_sub_ = this->create_subscription<sim_msgs::msg::FlightModelState>(
      "/aircraft/fdm/state", 10,
      [this](sim_msgs::msg::FlightModelState::ConstSharedPtr msg) {
        altitude_msl_m_ = msg->altitude_msl_m;
      });

    weather_sub_ = this->create_subscription<sim_msgs::msg::WeatherState>(
      "/world/weather", 10,
      [this](sim_msgs::msg::WeatherState::ConstSharedPtr msg) {
        // v2: absolute sea-level temp → compute deviation from ISA
        oat_deviation_k_ = msg->temperature_sl_k - ISA_T0;
        qnh_pa_ = msg->pressure_sl_pa;

        // Surface wind from first wind layer (if present)
        if (!msg->wind_layers.empty()) {
          const auto & wl = msg->wind_layers[0];
          double dir_rad = wl.wind_direction_deg * M_PI / 180.0;
          // Wind blows FROM dir, so NED components are opposite
          wind_north_ms_ = -static_cast<double>(wl.wind_speed_ms) * std::cos(dir_rad);
          wind_east_ms_  = -static_cast<double>(wl.wind_speed_ms) * std::sin(dir_rad);
          wind_down_ms_  = -static_cast<double>(wl.vertical_wind_ms);
        } else {
          wind_north_ms_ = 0.0;
          wind_east_ms_  = 0.0;
          wind_down_ms_  = 0.0;
        }
      });

    // ISA defaults until weather is received
    oat_deviation_k_ = 0.0;
    qnh_pa_ = ISA_P0;
    altitude_msl_m_ = 0.0;
    wind_north_ms_ = 0.0;
    wind_east_ms_  = 0.0;
    wind_down_ms_  = 0.0;

    RCLCPP_INFO(this->get_logger(), "atmosphere_node configured");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    atmo_pub_->on_activate();

    double rate_hz = this->get_parameter("update_rate_hz").as_double();
    int period_ms = static_cast<int>(1000.0 / rate_hz);

    publish_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(period_ms),
      [this]() { compute_and_publish(); });

    heartbeat_timer_ = this->create_wall_timer(
      std::chrono::seconds(1),
      [this]() {
        auto msg = std_msgs::msg::String();
        msg.data = this->get_name();
        heartbeat_pub_->publish(msg);
      });

    RCLCPP_INFO(this->get_logger(), "atmosphere_node activated (%.0f Hz)", rate_hz);
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    atmo_pub_->on_deactivate();
    publish_timer_.reset();
    heartbeat_timer_.reset();
    RCLCPP_INFO(this->get_logger(), "atmosphere_node deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    atmo_pub_.reset();
    heartbeat_pub_.reset();
    lifecycle_state_pub_.reset();
    flight_model_sub_.reset();
    weather_sub_.reset();
    RCLCPP_INFO(this->get_logger(), "atmosphere_node cleaned up");
    publish_lifecycle_state("unconfigured");
    return CallbackReturn::SUCCESS;
  }

private:
  // ─── ISA computation ──────────────────────────────────────────

  void compute_and_publish()
  {
    double alt = altitude_msl_m_;

    // Clamp altitude to valid ISA range
    alt = std::clamp(alt, -610.0, 20000.0);  // -2000 ft to 20 km

    double T_isa, P_isa;

    if (alt <= ISA_TROPO_ALT) {
      // Troposphere (0–11,000 m)
      T_isa = ISA_T0 - ISA_LAPSE * alt;
      P_isa = ISA_P0 * std::pow(T_isa / ISA_T0, ISA_G / (ISA_LAPSE * ISA_R));
      // The exponent g/(L*R) = 9.80665/(0.0065*287.058) ≈ 5.2561
    } else {
      // Tropopause / lower stratosphere (11,000–20,000 m)
      T_isa = ISA_T_TROPO;
      P_isa = ISA_P_TROPO * std::exp(-ISA_G / (ISA_R * ISA_T_TROPO) * (alt - ISA_TROPO_ALT));
      // The coefficient g/(R*T) = 9.80665/(287.058*216.65) ≈ 0.0001577
    }

    // Apply instructor OAT deviation to get actual temperature
    double oat_k = T_isa + oat_deviation_k_;
    double qnh = qnh_pa_;

    // Density uses actual OAT (not ISA) — affects TAS, dynamic pressure, density altitude
    double density = P_isa / (ISA_R * oat_k);
    double speed_of_sound = std::sqrt(ISA_GAMMA * ISA_R * oat_k);

    // Derived altitudes
    double pressure_altitude = HYPSO_SCALE * (1.0 - std::pow(P_isa / ISA_P0, HYPSO_P_EXP));
    double density_altitude  = HYPSO_SCALE * (1.0 - std::pow(density / ISA_RHO0, HYPSO_D_EXP));

    // Build and publish message
    auto msg = sim_msgs::msg::AtmosphereState();
    msg.header.stamp = this->now();
    msg.header.frame_id = "atmosphere";
    msg.temperature_k       = T_isa;
    msg.pressure_pa         = P_isa;
    msg.density_kgm3        = density;
    msg.speed_of_sound_ms   = speed_of_sound;
    msg.oat_k               = oat_k;
    msg.qnh_pa              = qnh;
    msg.density_altitude_m  = density_altitude;
    msg.pressure_altitude_m = pressure_altitude;

    // Wind at aircraft position (stub: surface wind only, no interpolation)
    msg.wind_north_ms       = wind_north_ms_;
    msg.wind_east_ms        = wind_east_ms_;
    msg.wind_down_ms        = wind_down_ms_;

    // Moisture and turbulence (stubs — proper computation in weather_solver)
    msg.visible_moisture     = false;
    msg.turbulence_intensity = 0.0f;

    atmo_pub_->publish(msg);
  }

  // ─── Member data ──────────────────────────────────────────────

  void publish_lifecycle_state(const std::string & state)
  {
    if (lifecycle_state_pub_) {
      auto msg = std_msgs::msg::String();
      msg.data = std::string(this->get_name()) + ":" + state;
      lifecycle_state_pub_->publish(msg);
    }
  }

  // Publishers
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::AtmosphereState>::SharedPtr atmo_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr heartbeat_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr lifecycle_state_pub_;

  // Subscriptions
  rclcpp::Subscription<sim_msgs::msg::FlightModelState>::SharedPtr flight_model_sub_;
  rclcpp::Subscription<sim_msgs::msg::WeatherState>::SharedPtr weather_sub_;

  // Timers
  rclcpp::TimerBase::SharedPtr publish_timer_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;

  // Cached inputs
  double altitude_msl_m_ = 0.0;
  double oat_deviation_k_ = 0.0;
  double qnh_pa_ = ISA_P0;
  double wind_north_ms_ = 0.0;
  double wind_east_ms_  = 0.0;
  double wind_down_ms_  = 0.0;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<AtmosphereNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
