#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <lifecycle_msgs/msg/state.hpp>
#include <std_msgs/msg/string.hpp>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/weather_state.hpp>
#include <sim_msgs/msg/atmosphere_state.hpp>

#include "weather_solver/weather_solver.hpp"

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class WeatherSolverNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  WeatherSolverNode()
  : LifecycleNode("weather_solver", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    this->declare_parameter<double>("update_rate_hz", 50.0);

    auto_start_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      [this]() {
        auto_start_timer_->cancel();
        auto_start_timer_.reset();
        auto st = this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE);
        if (st.id() != lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE) {
          RCLCPP_ERROR(this->get_logger(), "Auto-start: configure failed");
          return;
        }
        this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE);
      });
    RCLCPP_INFO(this->get_logger(), "weather_solver constructed (unconfigured)");
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    atmo_pub_ = this->create_publisher<sim_msgs::msg::AtmosphereState>(
      "/world/atmosphere", 10);
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle", 10);

    flight_model_sub_ = this->create_subscription<sim_msgs::msg::FlightModelState>(
      "/aircraft/fdm/state", 10,
      [this](sim_msgs::msg::FlightModelState::ConstSharedPtr msg) {
        altitude_msl_m_ = msg->altitude_msl_m;
        altitude_agl_m_ = msg->altitude_agl_m;
        tas_ms_     = msg->tas_ms;
        lat_deg_    = msg->latitude_deg;
        lon_deg_    = msg->longitude_deg;
        sim_time_s_ = msg->sim_time_sec;
      });

    weather_sub_ = this->create_subscription<sim_msgs::msg::WeatherState>(
      "/world/weather", 10,
      [this](sim_msgs::msg::WeatherState::ConstSharedPtr msg) {
        solver_.set_weather(*msg);
        RCLCPP_INFO(this->get_logger(),
          "Weather received: T=%.1fK P=%.0fPa vis=%.0fm wind_layers=%zu turb_model=%u seed=%u",
          msg->temperature_sl_k, msg->pressure_sl_pa, msg->visibility_m,
          msg->wind_layers.size(), msg->turbulence_model, msg->deterministic_seed);
      });

    weather_solver::WeatherSolver::Config cfg;
    cfg.update_rate_hz = this->get_parameter("update_rate_hz").as_double();
    solver_.configure(cfg);

    RCLCPP_INFO(this->get_logger(), "weather_solver configured");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    atmo_pub_->on_activate();

    double rate_hz = this->get_parameter("update_rate_hz").as_double();
    int period_ms = static_cast<int>(1000.0 / rate_hz);
    dt_sec_ = 1.0 / rate_hz;

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

    RCLCPP_INFO(this->get_logger(), "weather_solver activated (%.0f Hz)", rate_hz);
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    atmo_pub_->on_deactivate();
    publish_timer_.reset();
    heartbeat_timer_.reset();
    RCLCPP_INFO(this->get_logger(), "weather_solver deactivated");
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
    solver_.reset();
    RCLCPP_INFO(this->get_logger(), "weather_solver cleaned up");
    publish_lifecycle_state("unconfigured");
    return CallbackReturn::SUCCESS;
  }

private:
  void compute_and_publish()
  {
    auto r = solver_.compute(dt_sec_, altitude_msl_m_, altitude_agl_m_, tas_ms_,
                             lat_deg_, lon_deg_, sim_time_s_);

    auto msg = sim_msgs::msg::AtmosphereState();
    msg.header.stamp    = this->now();
    msg.header.frame_id = "atmosphere";
    msg.temperature_k       = r.temperature_k;
    msg.pressure_pa         = r.pressure_pa;
    msg.density_kgm3        = r.density_kgm3;
    msg.speed_of_sound_ms   = r.speed_of_sound_ms;
    msg.oat_k               = r.oat_k;
    msg.qnh_pa              = r.qnh_pa;
    msg.density_altitude_m  = r.density_altitude_m;
    msg.pressure_altitude_m = r.pressure_altitude_m;
    msg.wind_north_ms       = r.wind_north_ms;
    msg.wind_east_ms        = r.wind_east_ms;
    msg.wind_down_ms        = r.wind_down_ms;
    msg.visible_moisture     = r.visible_moisture;
    msg.turbulence_intensity = r.turbulence_intensity;
    msg.runway_condition_idx = r.runway_condition_idx;

    atmo_pub_->publish(msg);
  }

  void publish_lifecycle_state(const std::string & state)
  {
    if (lifecycle_state_pub_) {
      auto msg = std_msgs::msg::String();
      msg.data = std::string(this->get_name()) + ":" + state;
      lifecycle_state_pub_->publish(msg);
    }
  }

  weather_solver::WeatherSolver solver_;
  double dt_sec_ = 0.02;

  // Cached FDM inputs
  double altitude_msl_m_ = 0.0;
  double altitude_agl_m_ = 0.0;
  double tas_ms_          = 0.0;
  double lat_deg_         = 0.0;
  double lon_deg_         = 0.0;
  double sim_time_s_      = 0.0;

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
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<WeatherSolverNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
