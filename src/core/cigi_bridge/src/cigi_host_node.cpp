#include "cigi_bridge/cigi_host_node.hpp"
#include "cigi_session/ComponentIds.h"
#include <lifecycle_msgs/msg/state.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <fstream>
#include <lifecycle_msgs/msg/transition.hpp>

static constexpr double RAD_TO_DEG = 180.0 / M_PI;
static constexpr double DEG_TO_RAD = M_PI / 180.0;

// WGS-84 semi-major axis
static constexpr double EARTH_A = 6378137.0;

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

    // Register inbound dispatch targets with the session. Packets arrive via
    // session_.HandleDatagram(...) in recv_pending().
    session_.SetSofProcessor(this);
    session_.SetHatHotRespProcessor(this);

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
            // Sim is "frozen" for the IG whenever the host isn't advancing
            // the scene: instructor freeze OR reposition-pending. The IG
            // keeps rendering; this flag just tells the plugin it may do
            // work that would otherwise be visible (e.g. regen_weather).
            bool new_frozen = (sim_state_ == sim_msgs::msg::SimState::STATE_FROZEN)
                              || reposition_active_;
            if (new_frozen != sim_frozen_) {
                sim_frozen_ = new_frozen;
                RCLCPP_INFO(get_logger(), "Sim freeze state → %s",
                            sim_frozen_ ? "FROZEN" : "RUNNING");
            }
            if (reposition_active_ && !prev) {
                sent_reset_ = false;            // arm: send Reset on next frame
                hot_frame_counter_ = 0;         // fire HOT immediately on first reposition frame
                hat_tracker_.clear();           // discard in-flight HOT from old position
                weather_sync_.flush_on_reposition();  // IG will be reset — forget sent patches
                RCLCPP_INFO(get_logger(), "Reposition started — will send IG Reset");
            }
            if (!reposition_active_ && prev) {
                // Reposition complete — force re-emission of current weather state.
                // Global weather + patches will be sent as-new on the next frame.
                weather_dirty_ = true;
                RCLCPP_INFO(get_logger(), "Reposition complete — weather will re-emit");
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

    startup_reset_pending_ = true;

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
// Global weather packet emission — replaces the old weather_encoder. Appends
// an Atmosphere Control plus one Weather Control per cloud layer (always 3
// so removed layers are explicitly disabled on the IG), optional
// precipitation Weather Control, a Runway Friction Component Control, and
// wind-only Weather Controls (layer IDs 10+).
// ─────────────────────────────────────────────────────────────────────────────
namespace {

const sim_msgs::msg::WeatherWindLayer * find_nearest_wind(
    const std::vector<sim_msgs::msg::WeatherWindLayer> & layers,
    float altitude_m)
{
    if (layers.empty()) return nullptr;
    const sim_msgs::msg::WeatherWindLayer * best = &layers[0];
    float best_dist = std::abs(layers[0].altitude_msl_m - altitude_m);
    for (size_t i = 1; i < layers.size(); ++i) {
        float d = std::abs(layers[i].altitude_msl_m - altitude_m);
        if (d < best_dist) { best = &layers[i]; best_dist = d; }
    }
    return best;
}

}  // namespace

void CigiHostNode::append_global_weather(const sim_msgs::msg::WeatherState & weather)
{
    const float global_temp_c = static_cast<float>(weather.temperature_sl_k - 273.15);
    const float global_vis_m  = static_cast<float>(weather.visibility_m);
    const float global_baro   = static_cast<float>(weather.pressure_sl_pa / 100.0);

    // Surface wind from first wind layer (matches previous atmosphere encoder)
    float h_wind = 0.0f, v_wind = 0.0f, wind_dir = 0.0f;
    if (!weather.wind_layers.empty()) {
        h_wind   = weather.wind_layers[0].wind_speed_ms;
        v_wind   = weather.wind_layers[0].vertical_wind_ms;
        wind_dir = weather.wind_layers[0].wind_direction_deg;
    }

    // ── 1. Atmosphere Control ────────────────────────────────────────────
    cigi_session::HostSession::AtmosphereFields atm{};
    atm.humidity_pct            = static_cast<uint8_t>(std::clamp<int>(weather.humidity_pct, 0, 100));
    atm.temperature_c           = global_temp_c;
    atm.visibility_m            = global_vis_m;
    atm.horiz_wind_ms           = h_wind;
    atm.vert_wind_ms            = v_wind;
    atm.wind_direction_deg      = wind_dir;
    atm.barometric_pressure_hpa = global_baro;
    session_.AppendAtmosphereControl(atm);

    // Helper to build a default global Weather Control with scope=Global.
    auto make_global_wc = [&]() {
        cigi_session::HostSession::WeatherCtrlFields wc{};
        wc.region_id           = 0;
        wc.layer_id            = 0;
        wc.humidity_pct        = 0;
        wc.weather_enable      = true;
        wc.scud_enable         = false;
        wc.cloud_type          = 0;
        wc.scope               = cigi_session::HostSession::WeatherScope::Global;
        wc.severity            = 0;
        wc.air_temp_c          = global_temp_c;
        wc.visibility_m        = global_vis_m;
        wc.scud_frequency_pct  = 0.0f;
        wc.coverage_pct        = 0.0f;
        wc.base_elevation_m    = 0.0f;
        wc.thickness_m         = 0.0f;
        wc.transition_band_m   = 0.0f;
        wc.horiz_wind_ms       = 0.0f;
        wc.vert_wind_ms        = 0.0f;
        wc.wind_direction_deg  = 0.0f;
        wc.barometric_pressure_hpa   = global_baro;
        wc.aerosol_concentration_gm3 = 0.0f;
        return wc;
    };

    // ── 2. Cloud layers → always emit 3 packets (layer_id 1-3) ───────────
    const size_t num_clouds = std::min(weather.cloud_layers.size(), static_cast<size_t>(3));
    for (size_t i = 0; i < 3; ++i) {
        auto wc = make_global_wc();
        wc.layer_id = static_cast<uint8_t>(i + 1);
        if (i < num_clouds) {
            const auto & cl = weather.cloud_layers[i];
            const float center_alt = cl.base_elevation_m + cl.thickness_m * 0.5f;
            float lh = 0.0f, lv = 0.0f, ld = 0.0f, lturb = 0.0f;
            if (const auto * wl = find_nearest_wind(weather.wind_layers, center_alt)) {
                lh    = wl->wind_speed_ms;
                lv    = wl->vertical_wind_ms;
                ld    = wl->wind_direction_deg;
                lturb = wl->turbulence_severity;
            }
            wc.cloud_type          = cl.cloud_type;
            wc.scud_enable         = cl.scud_enable;
            wc.scud_frequency_pct  = cl.scud_frequency_pct;
            wc.coverage_pct        = cl.coverage_pct;
            wc.base_elevation_m    = cl.base_elevation_m;
            wc.thickness_m         = cl.thickness_m;
            wc.transition_band_m   = cl.transition_band_m;
            wc.horiz_wind_ms       = lh;
            wc.vert_wind_ms        = lv;
            wc.wind_direction_deg  = ld;
            wc.severity            = static_cast<uint8_t>(
                std::clamp(lturb * 5.0f, 0.0f, 5.0f));
            wc.weather_enable      = true;
        } else {
            // Explicit disable — layer not present
            wc.weather_enable = false;
        }
        session_.AppendWeatherControl(wc);
    }

    // ── 3. Precipitation layer (layer_id 4=Rain, 5=Snow) ─────────────────
    if (weather.precipitation_rate > 0.0f && weather.precipitation_type > 0) {
        auto wc = make_global_wc();
        wc.layer_id = (weather.precipitation_type == 2) ? 5 : 4;
        wc.coverage_pct = weather.precipitation_rate * 100.0f;
        float precip_thickness = 3000.0f;
        if (!weather.cloud_layers.empty()) {
            precip_thickness = weather.cloud_layers[0].base_elevation_m;
            if (precip_thickness < 100.0f) precip_thickness = 3000.0f;
        }
        wc.base_elevation_m = 0.0f;
        wc.thickness_m      = precip_thickness;
        session_.AppendWeatherControl(wc);
    }

    // ── 4. Runway friction (Component Control → GlobalTerrainSurface) ────
    // Previously a user-defined 0xCB packet; now a standard Component Control.
    session_.AppendComponentControl(
        cigi_session::HostSession::ComponentClass::GlobalTerrainSurface,
        /*instance_id=*/0,
        static_cast<std::uint16_t>(
            cigi_session::GlobalTerrainComponentId::RunwayFriction),
        /*component_state=*/weather.runway_condition_idx);

    // ── 5. Wind-only layers (layer_id 10+) ───────────────────────────────
    const size_t max_winds = std::min(weather.wind_layers.size(), static_cast<size_t>(13));
    for (size_t j = 0; j < max_winds; ++j) {
        const auto & wl = weather.wind_layers[j];
        auto wc = make_global_wc();
        wc.layer_id            = static_cast<uint8_t>(10 + j);
        wc.cloud_type          = 0;          // None
        wc.coverage_pct        = 0.0f;       // no cloud, wind only
        wc.base_elevation_m    = wl.altitude_msl_m;
        wc.thickness_m         = 0.0f;
        wc.horiz_wind_ms       = wl.wind_speed_ms;
        wc.vert_wind_ms        = wl.vertical_wind_ms;
        wc.wind_direction_deg  = wl.wind_direction_deg;
        wc.severity            = static_cast<uint8_t>(
            std::clamp(wl.turbulence_severity * 5.0f, 0.0f, 5.0f));
        session_.AppendWeatherControl(wc);
    }
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

    // IG Mode: send Standby for ONE frame when reposition starts (CIGI 3.3
    // collapses Reset and Standby to the same wire value), then Operate.
    uint8_t ig_mode = IG_MODE_OPERATE;
    if (reposition_active_ && !sent_reset_) {
        ig_mode = IG_MODE_STANDBY;
        sent_reset_ = true;
        RCLCPP_INFO(get_logger(), "Sending IG Mode = Reset (one frame, reposition)");
    } else if (startup_reset_pending_) {
        ig_mode = IG_MODE_STANDBY;
        startup_reset_pending_ = false;
        RCLCPP_INFO(get_logger(), "Sending IG Mode = Reset (one frame, startup)");
    }

    session_.BeginFrame(frame_counter_, ig_mode, timestamp_s);
    session_.AppendEntityCtrl(static_cast<uint16_t>(entity_id_),
                              roll_deg, pitch_deg, yaw_deg,
                              lat_deg, lon_deg, alt_m);

    // Sim freeze state → IG, every frame (idempotent). Under UDP a CompCtrl
    // emitted only on state transitions can be lost; re-asserting every
    // frame costs 32 bytes and guarantees the plugin converges.
    session_.AppendComponentControl(
        cigi_session::HostSession::ComponentClass::System,
        /*instance_id=*/0,
        static_cast<std::uint16_t>(cigi_session::SystemComponentId::SimFreezeState),
        /*component_state=*/sim_frozen_ ? 1 : 0);

    if (weather_dirty_) {
        append_global_weather(latest_weather_);
        weather_sync_.process_update(latest_weather_, session_);
        weather_dirty_ = false;
    }

    auto [buf, len] = session_.FinishFrame();
    if (send_fd_ >= 0 && buf != nullptr && len > 0) {
        sendto(send_fd_, buf, len, 0,
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

    // Emit a dedicated IG-Control-led datagram with all HOT requests. The
    // IG Control is required as the first packet of every Host→IG datagram
    // (CIGI 3.3 §4.1.1).
    hot_session_.BeginFrame(frame_counter_, IG_MODE_OPERATE, fms->sim_time_sec);

    for (auto & gp : gear_points_) {
        double pt_lat_deg, pt_lon_deg;
        body_to_latlon(ac_lat_rad, ac_lon_rad, heading_rad,
                       gp.x_m, gp.y_m, pt_lat_deg, pt_lon_deg);

        uint16_t req_id = hat_tracker_.next_id();
        hot_session_.AppendHatHotRequest(req_id, pt_lat_deg, pt_lon_deg);
        hat_tracker_.add_request(req_id, pt_lat_deg, pt_lon_deg, gp.name);
    }

    auto [buf, len] = hot_session_.FinishFrame();
    if (send_fd_ >= 0 && buf != nullptr && len > 0) {
        sendto(send_fd_, buf, len, 0,
               reinterpret_cast<struct sockaddr *>(&ig_addr_), sizeof(ig_addr_));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Drain pending UDP datagrams and hand each to the session for parsing.
// Dispatch arrives via OnSof / OnHatHotResp below.
// ─────────────────────────────────────────────────────────────────────────────
void CigiHostNode::recv_pending()
{
    if (recv_fd_ < 0) return;

    uint8_t buf[4096];
    while (true) {
        ssize_t n = ::recv(recv_fd_, buf, sizeof(buf), 0);
        if (n <= 0) break;  // EAGAIN or error
        session_.HandleDatagram(buf, static_cast<size_t>(n));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ISofProcessor — invoked by session_ for each SOF packet parsed.
// ─────────────────────────────────────────────────────────────────────────────
void CigiHostNode::OnSof(const cigi_session::SofFields & f)
{
    uint8_t prev = ig_status_;
    ig_status_     = static_cast<uint8_t>(f.ig_mode);
    last_ig_frame_ = f.ig_frame_number;
    if (ig_status_ != prev) {
        // CIGI 3.3 IG Mode enum: 0=Reset/Standby, 1=Operate, 2=Debug, 3=Offline
        static const char * mode_names[] = {"Standby", "Operate", "Debug", "Offline"};
        RCLCPP_INFO(get_logger(), "IG mode changed: %s → %s",
                    mode_names[prev & 0x03], mode_names[ig_status_ & 0x03]);
        auto status_msg = std_msgs::msg::UInt8();
        status_msg.data = ig_status_;
        ig_status_pub_->publish(status_msg);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IHatHotRespProcessor — invoked by session_ for each HAT/HOT Extended
// Response (packet 0x67) parsed.
// ─────────────────────────────────────────────────────────────────────────────
void CigiHostNode::OnHatHotResp(const cigi_session::HatHotRespFields & f)
{
    // Only publish HOT when IG reports terrain valid (Operate mode).
    if (ig_status_ != static_cast<uint8_t>(cigi_session::IgModeRx::Operate)) return;

    // Material code's low byte carries the framework surface enum.
    const uint8_t surface_type = static_cast<uint8_t>(f.material_code & 0xFF);

    auto resp = hat_tracker_.resolve(f.request_id, f.hat_m, f.hot_m, f.valid,
                                     surface_type);
    if (resp && hat_pub_->is_activated()) {
        hat_pub_->publish(*resp);
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
