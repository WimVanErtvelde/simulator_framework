#include "cigi_bridge/cigi_host_node.hpp"
#include "cigi_bridge/weather_encoder.hpp"
#include <lifecycle_msgs/msg/state.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <cstring>
#include <cmath>
#include <fstream>
#include <lifecycle_msgs/msg/transition.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Big-endian write helpers (CIGI 3.3 wire format is big-endian)
// ─────────────────────────────────────────────────────────────────────────────
static void write_be16(uint8_t * p, uint16_t v)
{
    p[0] = (v >> 8) & 0xFF;
    p[1] =  v       & 0xFF;
}

static void write_be32(uint8_t * p, uint32_t v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >>  8) & 0xFF;
    p[3] =  v        & 0xFF;
}

static void write_be_float(uint8_t * p, float v)
{
    uint32_t u;
    memcpy(&u, &v, sizeof u);
    write_be32(p, u);
}

static void write_be_double(uint8_t * p, double v)
{
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    p[0] = (u >> 56) & 0xFF; p[1] = (u >> 48) & 0xFF;
    p[2] = (u >> 40) & 0xFF; p[3] = (u >> 32) & 0xFF;
    p[4] = (u >> 24) & 0xFF; p[5] = (u >> 16) & 0xFF;
    p[6] = (u >>  8) & 0xFF; p[7] =  u         & 0xFF;
}

static constexpr double RAD_TO_DEG = 180.0 / M_PI;
static constexpr double DEG_TO_RAD = M_PI / 180.0;

// WGS-84 semi-major axis
static constexpr double EARTH_A = 6378137.0;

// ─────────────────────────────────────────────────────────────────────────────
// Minimal YAML value extractor (avoids full YAML dependency)
// Extracts double/string values from simple YAML sequences like gear_points
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
CigiHostNode::CigiHostNode()
: LifecycleNode("cigi_bridge", rclcpp::NodeOptions().parameter_overrides(
    {{"use_sim_time", true}}))
{
    declare_parameter("ig_address",       std::string("127.0.0.1"));
    declare_parameter("ig_port",          8002);
    declare_parameter("host_port",        8001);
    declare_parameter("entity_id",        0);
    declare_parameter("publish_rate_hz",  60.0);
    declare_parameter("aircraft_id",          std::string("c172"));
    declare_parameter("aircraft_config_path", std::string(""));

    auto_start_timer_ = create_wall_timer(
        std::chrono::milliseconds(100),
        [this]() {
            auto_start_timer_->cancel();
            auto_start_timer_.reset();
            auto st = trigger_transition(lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE);
            if (st.id() != lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE) {
                RCLCPP_ERROR(get_logger(), "Auto-start: configure failed — stays unconfigured");
                return;
            }
            trigger_transition(lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE);
        });

    RCLCPP_INFO(get_logger(), "cigi_bridge constructed");
}

// ─────────────────────────────────────────────────────────────────────────────
CallbackReturn CigiHostNode::on_configure(const rclcpp_lifecycle::State &)
{
    ig_address_       = get_parameter("ig_address").as_string();
    ig_port_          = get_parameter("ig_port").as_int();
    host_port_        = get_parameter("host_port").as_int();
    entity_id_        = get_parameter("entity_id").as_int();
    publish_rate_hz_  = get_parameter("publish_rate_hz").as_double();
    aircraft_id_      = get_parameter("aircraft_id").as_string();
    aircraft_config_path_ = get_parameter("aircraft_config_path").as_string();

    heartbeat_pub_  = create_publisher<std_msgs::msg::String>("/sim/diagnostics/heartbeat", 10);
    lifecycle_pub_  = create_publisher<std_msgs::msg::String>("/sim/diagnostics/lifecycle", 10);
    hat_pub_        = create_publisher<sim_msgs::msg::HatHotResponse>("/sim/cigi/hat_responses", 10);
    ig_status_pub_  = create_publisher<std_msgs::msg::UInt8>("/sim/cigi/ig_status", 10);
    alert_pub_      = create_publisher<sim_msgs::msg::SimAlert>("/sim/alerts", 10);

    if (!open_sockets()) {
        RCLCPP_ERROR(get_logger(), "cigi_bridge: failed to open UDP sockets (ig=%s:%d host_port=%d)",
                     ig_address_.c_str(), ig_port_, host_port_);
        auto alert = sim_msgs::msg::SimAlert();
        alert.severity = sim_msgs::msg::SimAlert::SEVERITY_CRITICAL;
        alert.source = "cigi_bridge";
        alert.message = "Failed to open UDP sockets (ig=" + ig_address_ + ":" + std::to_string(ig_port_) + ")";
        alert_pub_->publish(alert);
        return CallbackReturn::FAILURE;
    }

    load_gear_points();

    RCLCPP_INFO(get_logger(), "cigi_bridge configured → IG %s:%d  host_port %d  rate %.0f Hz  gear_points %zu",
                ig_address_.c_str(), ig_port_, host_port_, publish_rate_hz_, gear_points_.size());
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
CallbackReturn CigiHostNode::on_activate(const rclcpp_lifecycle::State &)
{
    hat_pub_->on_activate();

    fms_sub_ = create_subscription<sim_msgs::msg::FlightModelState>(
        "/aircraft/fdm/state", 10,
        [this](sim_msgs::msg::FlightModelState::SharedPtr msg) {
            std::lock_guard<std::mutex> lk(fms_mutex_);
            latest_fms_ = msg;
        });

    state_sub_ = create_subscription<sim_msgs::msg::SimState>(
        "/sim/state", 10,
        [this](sim_msgs::msg::SimState::SharedPtr msg) {
            sim_state_ = msg->state;
            bool prev = reposition_active_;
            reposition_active_ = msg->reposition_active;
            if (reposition_active_ && !prev) {
                sent_reset_ = false;       // arm: send Reset on next frame
                hot_frame_counter_ = 0;    // fire HOT immediately on first reposition frame
                hat_tracker_.clear();      // discard in-flight HOT from old position
                RCLCPP_INFO(get_logger(), "Reposition started — will send IG Reset");
            }
        });

    weather_sub_ = create_subscription<sim_msgs::msg::WeatherState>(
        "/world/weather", 10,
        [this](sim_msgs::msg::WeatherState::ConstSharedPtr msg) {
            latest_weather_ = *msg;
            weather_dirty_ = true;
        });

    auto period_ms = static_cast<int>(1000.0 / publish_rate_hz_);
    send_timer_ = create_wall_timer(
        std::chrono::milliseconds(period_ms),
        [this]() { send_cigi_frame(); send_hot_requests(); recv_pending(); });

    heartbeat_timer_ = create_wall_timer(
        std::chrono::seconds(1),
        [this]() {
            auto msg = std_msgs::msg::String();
            msg.data = get_name();
            heartbeat_pub_->publish(msg);
        });

    RCLCPP_INFO(get_logger(), "cigi_bridge activated");
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
CallbackReturn CigiHostNode::on_deactivate(const rclcpp_lifecycle::State &)
{
    send_timer_.reset();
    heartbeat_timer_.reset();
    fms_sub_.reset();
    state_sub_.reset();
    weather_sub_.reset();
    hat_pub_->on_deactivate();
    RCLCPP_INFO(get_logger(), "cigi_bridge deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
CallbackReturn CigiHostNode::on_cleanup(const rclcpp_lifecycle::State &)
{
    close_sockets();
    heartbeat_pub_.reset();
    lifecycle_pub_.reset();
    hat_pub_.reset();
    ig_status_pub_.reset();
    RCLCPP_INFO(get_logger(), "cigi_bridge cleaned up");
    publish_lifecycle_state("unconfigured");
    return CallbackReturn::SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// Gear point loading — simple text parsing of config.yaml gear_points section
// ─────────────────────────────────────────────────────────────────────────────
void CigiHostNode::load_gear_points()
{
    gear_points_.clear();

    // Resolve config path
    std::string path = aircraft_config_path_;
    if (path.empty()) {
        // Try common locations
        std::vector<std::string> candidates = {
            "src/aircraft/" + aircraft_id_ + "/config/config.yaml",
        };
        // Check AMENT_PREFIX_PATH
        const char* env = std::getenv("AMENT_PREFIX_PATH");
        if (env) {
            std::string paths(env);
            size_t pos = 0;
            while (pos < paths.size()) {
                size_t end = paths.find(':', pos);
                if (end == std::string::npos) end = paths.size();
                std::string prefix = paths.substr(pos, end - pos);
                candidates.insert(candidates.begin(),
                    prefix + "/share/aircraft_" + aircraft_id_ + "/config/config.yaml");
                pos = end + 1;
            }
        }
        for (auto & c : candidates) {
            std::ifstream f(c);
            if (f.good()) { path = c; break; }
        }
    }

    if (path.empty()) {
        RCLCPP_WARN(get_logger(), "No aircraft config found — no gear points for HOT requests");
        return;
    }

    // Simple line-by-line parse for gear_points section
    std::ifstream file(path);
    if (!file.is_open()) {
        RCLCPP_WARN(get_logger(), "Cannot open aircraft config: %s", path.c_str());
        return;
    }

    bool in_gear_points = false;
    GearPoint current;
    bool has_current = false;

    auto flush = [&]() {
        if (has_current && !current.name.empty()) {
            gear_points_.push_back(current);
        }
        current = {};
        has_current = false;
    };

    std::string line;
    while (std::getline(file, line)) {
        // Trim leading whitespace
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        std::string trimmed = line.substr(first);

        if (trimmed.find("gear_points:") == 0) {
            in_gear_points = true;
            continue;
        }
        if (!in_gear_points) continue;

        // Exit gear_points on unindented non-empty line
        if (first == 0 && !trimmed.empty() && trimmed[0] != '#') {
            flush();
            break;
        }

        // New list item
        if (trimmed.rfind("- name:", 0) == 0) {
            flush();
            has_current = true;
            auto colon = trimmed.find(':', 2);
            if (colon != std::string::npos) {
                std::string val = trimmed.substr(colon + 1);
                size_t vs = val.find_first_not_of(" \t");
                if (vs != std::string::npos) current.name = val.substr(vs);
            }
            continue;
        }

        // Parse key: value within current item
        auto colon = trimmed.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trimmed.substr(0, colon);
        // Trim key
        size_t ks = key.find_first_not_of(" \t-");
        size_t ke = key.find_last_not_of(" \t");
        if (ks == std::string::npos) continue;
        key = key.substr(ks, ke - ks + 1);

        std::string val = trimmed.substr(colon + 1);
        size_t vs = val.find_first_not_of(" \t");
        if (vs == std::string::npos) continue;
        val = val.substr(vs);
        // Strip trailing comment
        auto hash = val.find('#');
        if (hash != std::string::npos) val = val.substr(0, hash);
        // Trim trailing whitespace
        size_t ve = val.find_last_not_of(" \t\r\n");
        if (ve != std::string::npos) val = val.substr(0, ve + 1);

        if (!has_current) continue;

        try {
            if (key == "name") current.name = val;
            else if (key == "x_m") current.x_m = std::stod(val);
            else if (key == "y_m") current.y_m = std::stod(val);
            else if (key == "z_m") current.z_m = std::stod(val);
        } catch (...) {}
    }
    flush();

    for (auto & gp : gear_points_) {
        RCLCPP_INFO(get_logger(), "  gear_point: %s (%.2f, %.2f, %.2f)",
            gp.name.c_str(), gp.x_m, gp.y_m, gp.z_m);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Convert body-frame gear offset to geodetic lat/lon (degrees)
// Body frame: x=forward, y=right, z=up
// ─────────────────────────────────────────────────────────────────────────────
void CigiHostNode::body_to_latlon(double ac_lat_rad, double ac_lon_rad,
                                   double heading_rad, double x_m, double y_m,
                                   double & out_lat_deg, double & out_lon_deg)
{
    // Rotate body offsets to NED
    double cos_h = cos(heading_rad);
    double sin_h = sin(heading_rad);
    double north_m = x_m * cos_h - y_m * sin_h;
    double east_m  = x_m * sin_h + y_m * cos_h;

    // Convert to lat/lon offset (small angle approximation, fine for <100m offsets)
    double dlat_rad = north_m / EARTH_A;
    double dlon_rad = east_m / (EARTH_A * cos(ac_lat_rad));

    out_lat_deg = (ac_lat_rad + dlat_rad) * RAD_TO_DEG;
    out_lon_deg = (ac_lon_rad + dlon_rad) * RAD_TO_DEG;
}

// ─────────────────────────────────────────────────────────────────────────────
// Raw CIGI 3.3 packet encoding
// ─────────────────────────────────────────────────────────────────────────────

void CigiHostNode::encode_ig_ctrl(uint8_t * buf, uint32_t frame_cntr, double timestamp_s,
                                   uint8_t ig_mode) const
{
    memset(buf, 0, CIGI_IG_CTRL_SIZE);
    buf[0] = CIGI_PKT_IG_CTRL;
    buf[1] = CIGI_IG_CTRL_SIZE;
    buf[2] = 0;
    buf[3] = ig_mode;
    write_be32(&buf[8],  frame_cntr);
    write_be_double(&buf[16], timestamp_s);
}

void CigiHostNode::encode_entity_ctrl(uint8_t * buf, uint16_t entity_id,
                                      float roll_deg, float pitch_deg, float yaw_deg,
                                      double lat_deg, double lon_deg, double alt_m) const
{
    memset(buf, 0, CIGI_ENTITY_CTRL_SIZE);
    buf[0] = CIGI_PKT_ENTITY_CTRL;
    buf[1] = CIGI_ENTITY_CTRL_SIZE;
    write_be16(&buf[2], entity_id);
    buf[4]  = CIGI_ENTITY_ACTIVE;
    buf[5]  = 0;
    buf[10] = 0xFF;
    write_be_float(&buf[12], roll_deg);
    write_be_float(&buf[16], pitch_deg);
    write_be_float(&buf[20], yaw_deg);
    write_be_double(&buf[24], lat_deg);
    write_be_double(&buf[32], lon_deg);
    write_be_double(&buf[40], alt_m);
}

// HAT/HOT Request (Host → IG), Packet ID = 0x18, Size = 32 bytes
// CIGI 3.3 ICD Table 2-21
//   Byte 0:    Packet ID = 0x18
//   Byte 1:    Packet Size = 32
//   Byte 2-3:  HAT/HOT ID (uint16 BE)
//   Byte 4:    [1:0] Request Type (0=HAT, 1=HOT, 2=Extended),
//              [2] Coordinate System (0=geodetic), [3] Update Period, [7:4] Reserved
//   Byte 5-7:  Padding
//   Byte 8-9:  Entity ID (uint16 BE, 0 = ownship position not entity)
//   Byte 10-11:Padding
//   Byte 12-19:Latitude  (double BE, degrees)
//   Byte 20-27:Longitude (double BE, degrees)
//   Byte 28-31:Altitude  (float32 BE, metres — ignored for HOT, use 0)
void CigiHostNode::encode_hot_request(uint8_t * buf, uint16_t request_id,
                                      double lat_deg, double lon_deg) const
{
    memset(buf, 0, CIGI_HOT_REQUEST_SIZE);
    buf[0] = CIGI_PKT_HOT_REQUEST;
    buf[1] = CIGI_HOT_REQUEST_SIZE;
    write_be16(&buf[2], request_id);
    buf[4] = 0x01;  // Request Type = HOT (1), geodetic, one-shot
    write_be_double(&buf[12], lat_deg);
    write_be_double(&buf[20], lon_deg);
    write_be_float(&buf[28], 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
void CigiHostNode::send_cigi_frame()
{
    sim_msgs::msg::FlightModelState::SharedPtr fms;
    {
        std::lock_guard<std::mutex> lk(fms_mutex_);
        fms = latest_fms_;
    }

    double timestamp_s = 0.0;
    float  roll_deg = 0.0f, pitch_deg = 0.0f, yaw_deg = 0.0f;
    double lat_deg  = 0.0,  lon_deg   = 0.0,  alt_m   = 0.0;

    if (fms) {
        timestamp_s = fms->sim_time_sec;
        roll_deg    = static_cast<float>(fms->roll_rad  * RAD_TO_DEG);
        pitch_deg   = static_cast<float>(fms->pitch_rad * RAD_TO_DEG);
        yaw_deg     = static_cast<float>(fms->true_heading_rad * RAD_TO_DEG);
        lat_deg     = fms->latitude_deg;
        lon_deg     = fms->longitude_deg;
        alt_m       = fms->altitude_msl_m;
    }

    // IG Mode: send Reset for ONE frame when reposition starts, then Operate
    uint8_t ig_mode = CIGI_IG_MODE_OPERATE;
    if (reposition_active_ && !sent_reset_) {
        ig_mode = CIGI_IG_MODE_RESET;
        sent_reset_ = true;
        RCLCPP_INFO(get_logger(), "Sending IG Mode = Reset (one frame)");
    }

    // Build a single UDP datagram: IG Control + Entity Control + optional Weather
    // Max: 24 (IG) + 48 (Entity) + 32 (Atmo) + 56*17 (3 cloud + 1 precip + 13 wind) = 1056
    uint8_t datagram[1088];
    size_t offset = 0;

    encode_ig_ctrl(datagram, frame_counter_, timestamp_s, ig_mode);
    offset += CIGI_IG_CTRL_SIZE;

    encode_entity_ctrl(datagram + offset,
                       static_cast<uint16_t>(entity_id_),
                       roll_deg, pitch_deg, yaw_deg,
                       lat_deg, lon_deg, alt_m);
    offset += CIGI_ENTITY_CTRL_SIZE;

    // Append weather packets only when weather has changed
    if (weather_dirty_) {
        size_t weather_bytes = cigi_bridge::encode_weather_packets(
            datagram + offset, sizeof(datagram) - offset, latest_weather_);
        offset += weather_bytes;
        weather_dirty_ = false;
    }

    if (send_fd_ >= 0) {
        sendto(send_fd_, datagram, offset, 0,
               reinterpret_cast<struct sockaddr *>(&ig_addr_), sizeof(ig_addr_));
    }

    ++frame_counter_;
}

// ─────────────────────────────────────────────────────────────────────────────
// HOT request sending — rate gated by AGL altitude
//   <10m AGL: every frame (~50-60Hz)
//   10-100m AGL: every 6th frame (~10Hz at 60Hz)
//   >100m AGL: off
// ─────────────────────────────────────────────────────────────────────────────
void CigiHostNode::send_hot_requests()
{
    if (gear_points_.empty() || send_fd_ < 0) return;

    sim_msgs::msg::FlightModelState::SharedPtr fms;
    {
        std::lock_guard<std::mutex> lk(fms_mutex_);
        fms = latest_fms_;
    }
    if (!fms) return;

    double agl_m = fms->altitude_agl_m;

    if (reposition_active_) {
        // During reposition: 10Hz HOT regardless of AGL (terrain loading)
        uint32_t interval = static_cast<uint32_t>(publish_rate_hz_ / 10.0);
        if (interval < 1) interval = 1;
        ++hot_frame_counter_;
        if (hot_frame_counter_ < interval) return;
        hot_frame_counter_ = 0;
    } else {
        // Normal AGL-based rate gating
        if (agl_m > 100.0) {
            hot_frame_counter_ = 0;
            return;  // no HOT requests in cruise
        }

        uint32_t interval = (agl_m < 10.0) ? 1 : static_cast<uint32_t>(publish_rate_hz_ / 10.0);
        if (interval < 1) interval = 1;

        ++hot_frame_counter_;
        if (hot_frame_counter_ < interval) return;
        hot_frame_counter_ = 0;
    }

    double ac_lat_rad = fms->latitude_deg * DEG_TO_RAD;
    double ac_lon_rad = fms->longitude_deg * DEG_TO_RAD;
    double heading_rad = fms->true_heading_rad;

    // Build one UDP datagram with all HOT request packets
    size_t total = gear_points_.size() * CIGI_HOT_REQUEST_SIZE;
    std::vector<uint8_t> datagram(total, 0);

    for (size_t i = 0; i < gear_points_.size(); ++i) {
        auto & gp = gear_points_[i];
        double pt_lat_deg, pt_lon_deg;
        body_to_latlon(ac_lat_rad, ac_lon_rad, heading_rad,
                       gp.x_m, gp.y_m, pt_lat_deg, pt_lon_deg);

        uint16_t req_id = hat_tracker_.next_id();
        encode_hot_request(datagram.data() + i * CIGI_HOT_REQUEST_SIZE,
                           req_id, pt_lat_deg, pt_lon_deg);

        hat_tracker_.add_request(req_id,
                                 pt_lat_deg,
                                 pt_lon_deg,
                                 gp.name);
    }

    if (send_fd_ >= 0) {
        sendto(send_fd_, datagram.data(), total, 0,
               reinterpret_cast<struct sockaddr *>(&ig_addr_), sizeof(ig_addr_));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// HAT/HOT response wire format (IG → Host), Packet ID = 0x02
// Standard CIGI 3.3: 16 bytes
//   Byte 0:   Packet ID = 2
//   Byte 1:   Packet Size = 16
//   Byte 2-3: HAT/HOT ID (uint16 BE)
//   Byte 4:   [0] Valid, [1] Type (0=HOT, 1=HAT), [7:2] Reserved
//   Byte 5-7: Reserved
//   Byte 8-15:HOT or HAT value (double BE, metres)
//
// Extended response (48 bytes) from our X-Plane plugin:
//   Byte 0:   Packet ID = 2
//   Byte 1:   48
//   Byte 2-3: Request ID (uint16 BE)
//   Byte 4:   [0] Valid
//   Byte 8-15:HOT (double BE, metres MSL)
//   Byte 16-23:HAT (double BE, metres)
void CigiHostNode::recv_pending()
{
    if (recv_fd_ < 0) return;

    uint8_t buf[4096];
    while (true) {
        ssize_t n = ::recv(recv_fd_, buf, sizeof(buf), 0);
        if (n <= 0) break;  // EAGAIN or error

        // Walk the CIGI packet stream in the datagram
        ssize_t offset = 0;
        while (offset + 2 <= n) {
            uint8_t pkt_id   = buf[offset];
            uint8_t pkt_size = buf[offset + 1];
            if (pkt_size < 2 || offset + pkt_size > n) break;

            if (pkt_id == 0x02 && pkt_size >= 16) {
                // HAT/HOT Response (standard 16-byte or extended 48-byte)
                uint16_t hat_hot_id  = (static_cast<uint16_t>(buf[offset + 2]) << 8) | buf[offset + 3];
                uint8_t  flags       = buf[offset + 4];
                bool     valid       = (flags & 0x01) != 0;

                // Read HOT value (bytes 8-15 in both standard and extended)
                uint64_t val_u = 0;
                for (int i = 0; i < 8; ++i)
                    val_u = (val_u << 8) | buf[offset + 8 + i];
                double hot_m;
                memcpy(&hot_m, &val_u, 8);

                // Only publish HOT when IG reports terrain valid
                if (ig_status_ != CIGI_SOF_IG_STATUS_OPERATE) { offset += pkt_size; continue; }
                auto resp = hat_tracker_.resolve(hat_hot_id, hot_m, valid);
                if (resp && hat_pub_->is_activated()) {
                    hat_pub_->publish(*resp);
                }
            }
            else if (pkt_id == 0x01 && pkt_size >= 8) {
                // SOF (Start of Frame) from IG — parse IG Mode
                // CIGI 3.3 ICD: byte 4 bits[1:0] = IG Mode (0=Standby, 1=Reset, 2=Operate)
                uint8_t prev = ig_status_;
                ig_status_ = buf[offset + 4] & 0x03;
                if (ig_status_ != prev) {
                    static const char * mode_names[] = {"Standby", "Reset", "Operate", "Debug"};
                    RCLCPP_INFO(get_logger(), "IG status changed: %s → %s",
                                mode_names[prev & 0x03], mode_names[ig_status_ & 0x03]);
                    auto status_msg = std_msgs::msg::UInt8();
                    status_msg.data = ig_status_;
                    ig_status_pub_->publish(status_msg);
                }
            }
            offset += pkt_size;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
bool CigiHostNode::open_sockets()
{
    send_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (send_fd_ < 0) return false;
    memset(&ig_addr_, 0, sizeof ig_addr_);
    ig_addr_.sin_family = AF_INET;
    ig_addr_.sin_port   = htons(static_cast<uint16_t>(ig_port_));
    if (inet_pton(AF_INET, ig_address_.c_str(), &ig_addr_.sin_addr) != 1) {
        RCLCPP_ERROR(get_logger(), "cigi_bridge: invalid ig_address '%s'", ig_address_.c_str());
        close(send_fd_);
        send_fd_ = -1;
        return false;
    }

    recv_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (recv_fd_ < 0) return false;
    struct sockaddr_in bind_addr {};
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port        = htons(static_cast<uint16_t>(host_port_));
    if (bind(recv_fd_, reinterpret_cast<struct sockaddr *>(&bind_addr), sizeof bind_addr) < 0) {
        RCLCPP_WARN(get_logger(), "cigi_bridge: bind on host_port %d failed — recv disabled", host_port_);
        close(recv_fd_);
        recv_fd_ = -1;
    } else {
        fcntl(recv_fd_, F_SETFL, fcntl(recv_fd_, F_GETFL, 0) | O_NONBLOCK);
    }
    return true;
}

void CigiHostNode::close_sockets()
{
    if (send_fd_ >= 0) { close(send_fd_); send_fd_ = -1; }
    if (recv_fd_ >= 0) { close(recv_fd_); recv_fd_ = -1; }
}

void CigiHostNode::publish_lifecycle_state(const std::string & state)
{
    if (!lifecycle_pub_) return;
    auto msg = std_msgs::msg::String();
    msg.data = std::string(get_name()) + ":" + state;
    lifecycle_pub_->publish(msg);
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<CigiHostNode>();
    rclcpp::spin(node->get_node_base_interface());
    rclcpp::shutdown();
    return 0;
}
