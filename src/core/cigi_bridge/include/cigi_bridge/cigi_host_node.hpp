#pragma once
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/u_int8.hpp>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/hat_hot_response.hpp>
#include <sim_msgs/msg/sim_state.hpp>
#include <sim_msgs/msg/sim_alert.hpp>
#include <sim_msgs/msg/weather_state.hpp>

#include "cigi_bridge/hat_request_tracker.hpp"
#include "cigi_bridge/weather_sync.hpp"
#include "cigi_session/HostSession.h"
#include "cigi_session/processors/ISofProcessor.h"
#include "cigi_session/processors/IHatHotRespProcessor.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <mutex>
#include <vector>
#include <string>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

// CIGI 3.3 host-side ROS2 lifecycle node.
//
// Sends Entity Control + IG Control packets to the Image Generator (X-Plane)
// over UDP. Datagram assembly goes through cigi_session::HostSession which
// wraps Boeing's CCL 3.3.3 (CigiHostSession) for wire-format translation.
// Receives SOF and HAT/HOT response packets via the same session (dispatched
// to OnSof / OnHatHotResp). HAT results are published on
// /sim/cigi/hat_responses.
//
// Sign convention passed to the IG (matches X-Plane plugin EntityCtrl.cpp):
//   pitch > 0 → nose up   (plugin negates for X-Plane phi)
//   roll  > 0 → right bank(plugin negates for X-Plane theta)
//   yaw   degrees 0-360 true heading (plugin passes straight to X-Plane psi)
class CigiHostNode : public rclcpp_lifecycle::LifecycleNode,
                     public cigi_session::ISofProcessor,
                     public cigi_session::IHatHotRespProcessor
{
public:
    CigiHostNode();

    CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
    CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
    CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
    CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;

    // ── cigi_session inbound dispatch ──────────────────────────────────────
    void OnSof(const cigi_session::SofFields & f) override;
    void OnHatHotResp(const cigi_session::HatHotRespFields & f) override;

private:
    // IG Mode wire values passed to HostSession::BeginFrame.
    //   0 = Standby/Reset (CIGI 3.3 collapses these)
    //   1 = Operate
    //   2 = Debug
    //   3 = Offline
    static constexpr std::uint8_t IG_MODE_STANDBY = 0;
    static constexpr std::uint8_t IG_MODE_OPERATE = 1;

    // ── Gear point config ─────────────────────────────────────────────────
    struct GearPoint {
        std::string name;
        double x_m;   // forward of CG (body frame)
        double y_m;   // right of CG (body frame)
        double z_m;   // up from CG (body frame)
    };
    std::vector<GearPoint> gear_points_;

    void load_gear_points();
    static void body_to_latlon(double ac_lat_rad, double ac_lon_rad,
                               double heading_rad, double x_m, double y_m,
                               double & out_lat_deg, double & out_lon_deg);

    // ── Send / receive ────────────────────────────────────────────────────
    void send_cigi_frame();
    void send_hot_requests();
    void recv_pending();

    // Append global weather (Atmosphere + Weather Control packets) to the
    // current session frame. Mirrors the packet sequence the framework
    // previously emitted via weather_encoder.
    void append_global_weather(const sim_msgs::msg::WeatherState & weather);

    // ── Socket management ─────────────────────────────────────────────────
    bool open_sockets();
    void close_sockets();

    // ── Lifecycle helpers ─────────────────────────────────────────────────
    void publish_lifecycle_state(const std::string & state);

    // ── Parameters (set in on_configure) ─────────────────────────────────
    std::string ig_address_;
    int         ig_port_          = 8002;
    int         host_port_        = 8001;
    int         entity_id_        = 0;
    double      publish_rate_hz_  = 60.0;
    std::string aircraft_id_       = "c172";
    std::string aircraft_config_path_;

    // ── Sockets ───────────────────────────────────────────────────────────
    int send_fd_ = -1;
    int recv_fd_ = -1;
    struct sockaddr_in ig_addr_ {};

    // ── State ─────────────────────────────────────────────────────────────
    sim_msgs::msg::FlightModelState::SharedPtr latest_fms_;
    std::mutex fms_mutex_;
    uint32_t   frame_counter_ = 0;
    uint32_t   last_ig_frame_ = 0;   // last IG Frame Number from SOF, echoed in IG Control
    uint8_t    ig_status_     = 0;   // SOF IG Mode from IG: 0=Standby, 1=Operate, 2=Debug
    uint8_t    sim_state_     = 0;   // from /sim/state
    bool       reposition_active_ = false;  // from SimState.reposition_active
    bool       sent_reset_    = false;      // true after Reset sent, cleared on next frame
    bool       startup_reset_pending_ = false;  // send IG Mode=Reset once on first frame after activate

    // ── HOT rate gating ───────────────────────────────────────────────────
    uint32_t hot_frame_counter_ = 0;  // counts frames since last HOT send

    // ── HAT tracking ──────────────────────────────────────────────────────
    HatRequestTracker hat_tracker_;

    // ── CIGI session (wraps CCL CigiHostSession) ──────────────────────────
    cigi_session::HostSession session_;
    // Separate session for HOT-request datagrams (one per frame, independent
    // of the main IG Control + Entity + Weather datagram). HOT requests are
    // emitted as their own CIGI 3.3 message so they can be rate-gated by AGL.
    cigi_session::HostSession hot_session_;

    // ── Weather sync (regional patch diff tracking) ──────────────────────
    cigi_bridge::WeatherSync weather_sync_;

    // ── Weather state ────────────────────────────────────────────────────
    sim_msgs::msg::WeatherState latest_weather_;
    bool weather_dirty_ = false;

    // ── ROS2 interfaces ───────────────────────────────────────────────────
    rclcpp::Subscription<sim_msgs::msg::FlightModelState>::SharedPtr      fms_sub_;
    rclcpp::Subscription<sim_msgs::msg::SimState>::SharedPtr              state_sub_;
    rclcpp::Subscription<sim_msgs::msg::WeatherState>::SharedPtr          weather_sub_;
    rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::HatHotResponse>::SharedPtr hat_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr heartbeat_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr lifecycle_pub_;
    rclcpp::Publisher<std_msgs::msg::UInt8>::SharedPtr ig_status_pub_;
    rclcpp::Publisher<sim_msgs::msg::SimAlert>::SharedPtr alert_pub_;

    rclcpp::TimerBase::SharedPtr send_timer_;
    rclcpp::TimerBase::SharedPtr heartbeat_timer_;
    rclcpp::TimerBase::SharedPtr auto_start_timer_;
};
