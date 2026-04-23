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

#include <sys/socket.h>
#include <netinet/in.h>
#include <mutex>
#include <vector>
#include <string>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

// CIGI 3.3 host-side ROS2 lifecycle node.
//
// Sends Entity Control + IG Control packets to the Image Generator (X-Plane)
// over UDP using raw CIGI 3.3 encoding (no CCL dependency).
// Receives SOF and HAT/HOT response packets; publishes HAT results to
// /sim/cigi/hat_responses.
//
// Sign convention passed to the IG (matches X-Plane plugin EntityCtrl.cpp):
//   pitch > 0 → nose up   (plugin negates for X-Plane phi)
//   roll  > 0 → right bank(plugin negates for X-Plane theta)
//   yaw   degrees 0-360 true heading (plugin passes straight to X-Plane psi)
class CigiHostNode : public rclcpp_lifecycle::LifecycleNode
{
public:
    CigiHostNode();

    CallbackReturn on_configure(const rclcpp_lifecycle::State &) override;
    CallbackReturn on_activate(const rclcpp_lifecycle::State &) override;
    CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override;
    CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override;

private:
    // ── CIGI raw packet encoding ──────────────────────────────────────────
    // Host→IG packet IDs
    static constexpr uint8_t CIGI_PKT_IG_CTRL      = 0x01;
    static constexpr uint8_t CIGI_PKT_ENTITY_CTRL   = 0x02;
    static constexpr uint8_t CIGI_PKT_HOT_REQUEST   = 0x18;
    // IG→Host packet IDs (CIGI 3.3 ICD §2.2.2 Table 1)
    static constexpr uint8_t CIGI_PKT_SOF                  = 0x65;  // 101
    static constexpr uint8_t CIGI_PKT_HAT_HOT_EXT_RESPONSE = 0x67;  // 103 — carries both HAT and HOT + material code (§4.2.3, 40 B)
    static constexpr uint8_t CIGI_IG_CTRL_SIZE      = 24;
    static constexpr uint8_t CIGI_ENTITY_CTRL_SIZE  = 48;
    static constexpr uint8_t CIGI_HOT_REQUEST_SIZE  = 32;

    // IG mode byte[3] bits[1:0] = IG Mode, bit[2] = TimestampValid
    static constexpr uint8_t CIGI_IG_MODE_RESET   = 0x05;  // Reset(1) | TimestampValid → 0x05
    static constexpr uint8_t CIGI_IG_MODE_OPERATE = 0x06;  // Operate(2) | TimestampValid → 0x06
    // Entity state byte[4]: Active(1) in bits[1:0]
    static constexpr uint8_t CIGI_ENTITY_ACTIVE = 0x01;
    // SOF IG Status byte[4] bits[1:0] (incoming, different encoding from outgoing IG Mode)
    static constexpr uint8_t CIGI_SOF_IG_STATUS_OPERATE = 0x02;

    void encode_ig_ctrl(uint8_t * buf, uint32_t frame_cntr, double timestamp_s,
                        uint8_t ig_mode) const;
    void encode_entity_ctrl(uint8_t * buf, uint16_t entity_id,
                            float roll_deg, float pitch_deg, float yaw_deg,
                            double lat_deg, double lon_deg, double alt_m) const;
    void encode_hot_request(uint8_t * buf, uint16_t request_id,
                            double lat_deg, double lon_deg) const;

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
    uint8_t    ig_status_     = 0;   // SOF IG Status from IG: 0=Standby, 1=Reset, 2=Operate
    uint8_t    sim_state_     = 0;   // from /sim/state
    bool       reposition_active_ = false;  // from SimState.reposition_active
    bool       sent_reset_    = false;      // true after Reset sent, cleared on next frame
    bool       startup_reset_pending_ = false;  // send IG Mode=Reset once on first frame after activate

    // ── HOT rate gating ───────────────────────────────────────────────────
    uint32_t hot_frame_counter_ = 0;  // counts frames since last HOT send

    // ── HAT tracking ──────────────────────────────────────────────────────
    HatRequestTracker hat_tracker_;

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
