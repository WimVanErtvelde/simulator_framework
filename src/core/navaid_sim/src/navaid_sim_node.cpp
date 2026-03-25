#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <std_msgs/msg/string.hpp>
#include <sim_msgs/msg/flight_model_state.hpp>
#include <sim_msgs/msg/avionics_controls.hpp>
#include <sim_msgs/msg/nav_signal_table.hpp>
#include <sim_msgs/msg/failure_injection.hpp>
#include <sim_msgs/srv/get_terrain_elevation.hpp>
#include <sim_msgs/srv/search_airports.hpp>
#include <sim_msgs/srv/get_runways.hpp>
#include <sim_msgs/srv/search_navaids.hpp>
#include <sim_msgs/msg/airport.hpp>
#include <sim_msgs/msg/runway.hpp>

#include <set>
#include <algorithm>
#include <cctype>

#include "World.h"
#include "Model.h"
#include "NavSimTask.h"
#include "WorldParser.h"
#include "A424Parser.h"
#include "MagDec.h"
#include "Units.h"
#include "AirportDatabase.h"

#include <cmath>
#include <memory>
#include <sys/stat.h>

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

static constexpr double DEG_TO_RAD = M_PI / 180.0;
static constexpr double NM_TO_M = 1852.0;

class NavaidSimNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  NavaidSimNode()
  : LifecycleNode("navaid_sim", rclcpp::NodeOptions().parameter_overrides(
      {{"use_sim_time", true}}))
  {
    this->declare_parameter<std::string>("navdb_path", "");
    this->declare_parameter<std::string>("navdb_format", "xp12");  // "xp12" or "a424"
    this->declare_parameter<std::string>("terrain_dir", "");
    this->declare_parameter<std::string>("magdec_path", "");
    this->declare_parameter<std::string>("airport_db_path", "");
    this->declare_parameter<std::string>("airport_db_format", "a424");  // "a424" or "xp12"
    this->declare_parameter<double>("update_rate_hz", 10.0);

    auto_start_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      [this]() {
        auto_start_timer_->cancel();
        auto_start_timer_.reset();
        this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_CONFIGURE);
        this->trigger_transition(
          lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE);
      });
    RCLCPP_INFO(this->get_logger(), "navaid_sim constructed (unconfigured)");
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    // Publishers
    heartbeat_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/heartbeat", 10);
    lifecycle_state_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/sim/diagnostics/lifecycle_state", 10);
    nav_signals_pub_ = this->create_publisher<sim_msgs::msg::NavSignalTable>(
      "/sim/world/nav_signals", 10);

    // Subscriptions
    flight_model_sub_ = this->create_subscription<sim_msgs::msg::FlightModelState>(
      "/sim/flight_model/state", 10,
      [this](const sim_msgs::msg::FlightModelState::SharedPtr msg) {
        last_flight_model_state_ = *msg;
        flight_model_received_ = true;
      });

    avionics_sub_ = this->create_subscription<sim_msgs::msg::AvionicsControls>(
      "/sim/controls/avionics", 10,
      [this](const sim_msgs::msg::AvionicsControls::SharedPtr msg) {
        last_avionics_ = *msg;
      });

    // Failure injection subscription (reliable QoS)
    auto reliable_qos = rclcpp::QoS(10).reliable();
    failure_injection_sub_ = this->create_subscription<sim_msgs::msg::FailureInjection>(
      "/sim/failure/navaid_commands", reliable_qos,
      [this](const sim_msgs::msg::FailureInjection::SharedPtr msg) {
        on_failure_injection(msg);
      });

    // Resolve data paths
    std::string pkg_share;
    try {
      // Try ament index first
      pkg_share = ament_index_cpp_get_share("navaid_sim");
    } catch (...) {
      pkg_share = "";
    }

    auto navdb_path = this->get_parameter("navdb_path").as_string();
    auto navdb_format = this->get_parameter("navdb_format").as_string();
    auto terrain_dir = this->get_parameter("terrain_dir").as_string();
    auto magdec_path = this->get_parameter("magdec_path").as_string();

    // Default paths — try package share, then source tree
    if (navdb_path.empty()) {
      if (!pkg_share.empty()) {
        navdb_path = pkg_share + "/data/earth_nav.dat";
      } else {
        navdb_path = "src/core/navaid_sim/data/earth_nav.dat";
      }
    }
    if (terrain_dir.empty()) {
      if (!pkg_share.empty()) {
        terrain_dir = pkg_share + "/data/srtm3/";
      } else {
        terrain_dir = "src/core/navaid_sim/data/srtm3/";
      }
    }
    if (magdec_path.empty()) {
      if (!pkg_share.empty()) {
        magdec_path = pkg_share + "/data/WMM.COF";
      } else {
        magdec_path = "src/core/navaid_sim/data/WMM.COF";
      }
    }

    // Load navaid database
    world_ = std::make_unique<AS::World>();
    model_ = std::make_unique<AS::Model>();

    if (navdb_format == "a424") {
      if (!A424::A424Parser::ParseA424(navdb_path, world_.get())) {
        RCLCPP_ERROR(this->get_logger(), "Failed to load navaid database: %s", navdb_path.c_str());
        return CallbackReturn::FAILURE;
      }
    } else {
      magdec_ = std::make_unique<MagDec>();
      if (!magdec_->load(magdec_path)) {
        RCLCPP_WARN(this->get_logger(), "Failed to load magdec: %s — bearings may be inaccurate",
                    magdec_path.c_str());
      }
      AS::WorldParser parser;
      parser.parseXP12(navdb_path, *world_,
                       magdec_ && magdec_->isLoaded() ? magdec_.get() : nullptr);
    }

    // Create simulation task (wires receivers + terrain LOS)
    task_ = std::make_unique<NavSimTask>(world_.get(), model_.get(), terrain_dir);

    // Terrain elevation service
    terrain_srv_ = this->create_service<sim_msgs::srv::GetTerrainElevation>(
      "/navaid_sim/get_terrain_elevation",
      [this](const sim_msgs::srv::GetTerrainElevation::Request::SharedPtr req,
             sim_msgs::srv::GetTerrainElevation::Response::SharedPtr resp) {
        if (!task_ || !task_->getTerrainModel().isAvailable()) {
          resp->valid = false;
          resp->elevation_msl_m = 0.0;
          return;
        }
        double lat_deg = req->latitude_deg;
        double lon_deg = req->longitude_deg;
        if (task_->getTerrainModel().hasTile(lat_deg, lon_deg)) {
          resp->elevation_msl_m = task_->getTerrainModel().getElevationM(lat_deg, lon_deg);
          resp->valid = true;
        } else {
          resp->elevation_msl_m = 0.0;
          resp->valid = false;
        }
      });

    // ── Airport database ──────────────────────────────────────────────
    auto apt_format = this->get_parameter("airport_db_format").as_string();
    auto apt_path = this->get_parameter("airport_db_path").as_string();

    if (apt_path.empty()) {
      // Default: same file as navdb for a424, separate apt.dat for xp12
      if (apt_format == "a424") {
        apt_path = navdb_path;  // euramec.pc contains both navaids and airports
      } else {
        if (!pkg_share.empty()) {
          apt_path = pkg_share + "/data/apt.dat";
        } else {
          apt_path = "src/core/navaid_sim/data/apt.dat";
        }
      }
    }

    airport_db_ = std::make_unique<AirportDatabase>();
    bool apt_ok = false;
    if (apt_format == "a424") {
      apt_ok = airport_db_->loadA424(apt_path);
    } else {
      apt_ok = airport_db_->loadXP12(apt_path);
    }

    if (!apt_ok) {
      RCLCPP_WARN(this->get_logger(), "Failed to load airport database: %s", apt_path.c_str());
    }

    // Airport search service
    search_airports_srv_ = this->create_service<sim_msgs::srv::SearchAirports>(
      "/navaid_sim/search_airports",
      [this](const sim_msgs::srv::SearchAirports::Request::SharedPtr req,
             sim_msgs::srv::SearchAirports::Response::SharedPtr resp) {
        if (!airport_db_) return;
        size_t max_r = req->max_results > 0 ? req->max_results : 10;
        auto results = airport_db_->search(req->query, max_r);
        for (auto * apt : results) {
          resp->airports.push_back(airport_to_msg(*apt));
        }
      });

    // Get runways service
    get_runways_srv_ = this->create_service<sim_msgs::srv::GetRunways>(
      "/navaid_sim/get_runways",
      [this](const sim_msgs::srv::GetRunways::Request::SharedPtr req,
             sim_msgs::srv::GetRunways::Response::SharedPtr resp) {
        if (!airport_db_) { resp->found = false; return; }
        auto * apt = airport_db_->findByICAO(req->icao);
        if (apt) {
          resp->found = true;
          resp->airport = airport_to_msg(*apt);
        } else {
          resp->found = false;
        }
      });

    // Search navaids service
    search_navaids_srv_ = this->create_service<sim_msgs::srv::SearchNavaids>(
      "/navaid_sim/search_navaids",
      [this](const sim_msgs::srv::SearchNavaids::Request::SharedPtr req,
             sim_msgs::srv::SearchNavaids::Response::SharedPtr resp) {
        if (!world_) return;
        std::string query = req->query;
        for (auto & c : query) c = static_cast<char>(std::toupper(c));
        if (query.size() < 2) return;

        size_t max_r = req->max_results > 0 ? req->max_results : 20;
        std::set<std::string> type_filter;
        if (!req->types.empty()) {
          std::istringstream ss(req->types);
          std::string t;
          while (std::getline(ss, t, ',')) {
            for (auto & c : t) c = static_cast<char>(std::toupper(c));
            auto trimmed = t.substr(t.find_first_not_of(' ') == std::string::npos ? 0 : t.find_first_not_of(' '));
            if (!trimmed.empty()) type_filter.insert(trimmed);
          }
        }

        auto push = [&](const std::string & ident, const std::string & name,
                        const std::string & type, double lat, double lon,
                        float freq_mhz, float range) {
          if (!type_filter.empty() && type_filter.find(type) == type_filter.end()) return;
          std::string uident = ident;
          for (auto & c : uident) c = static_cast<char>(std::toupper(c));
          std::string uname = name;
          for (auto & c : uname) c = static_cast<char>(std::toupper(c));
          bool is_match = (uident.find(query) == 0) ||
                          (uname.find(query) != std::string::npos);
          if (!is_match) return;
          if (resp->idents.size() >= max_r) return;
          resp->idents.push_back(ident);
          resp->names.push_back(name);
          resp->types.push_back(type);
          resp->latitudes.push_back(lat);
          resp->longitudes.push_back(lon);
          resp->frequencies_mhz.push_back(freq_mhz);
          resp->ranges_nm.push_back(range);
        };

        for (auto & [f, v] : world_->allVORs()) {
          if (resp->idents.size() >= max_r) break;
          push(v.mIdent, v.mName, "VOR",
               v.mLatLon.get_lat_deg(), v.mLatLon.get_lon_deg(),
               static_cast<float>(v.mFrequency) / 100.0f, v.mRange);
        }
        for (auto & [f, n] : world_->allNDBs()) {
          if (resp->idents.size() >= max_r) break;
          push(n.mIdent, n.mName, "NDB",
               n.mLatLon.get_lat_deg(), n.mLatLon.get_lon_deg(),
               static_cast<float>(n.mFrequency), n.mRange);
        }
        for (auto & [f, l] : world_->allLOCs()) {
          if (resp->idents.size() >= max_r) break;
          push(l.mIdent, l.mName, "ILS",
               l.mLatLon.get_lat_deg(), l.mLatLon.get_lon_deg(),
               static_cast<float>(l.mFrequency) / 100.0f, l.mRange);
        }
      });

    // ── Startup summary ─────────────────────────────────────────────
    RCLCPP_INFO(this->get_logger(), "========== navaid_sim configured ==========");
    RCLCPP_INFO(this->get_logger(), "  NavDB:   %s (%s)",
      navdb_path.c_str(), navdb_format == "a424" ? "ARINC-424" : "X-Plane");
    if (navdb_format == "a424") {
      auto hdr = A424::A424Parser::ReadHeader(navdb_path);
      if (hdr.valid) {
        RCLCPP_INFO(this->get_logger(), "  Cycle:   %s  Created: %s  Supplier: %s",
          hdr.cycle.c_str(), hdr.created.c_str(), hdr.supplier.c_str());
      }
    } else {
      RCLCPP_INFO(this->get_logger(), "  MagDec:  %s (%s)",
        magdec_path.c_str(), (magdec_ && magdec_->isLoaded()) ? "loaded" : "NOT loaded");
    }
    RCLCPP_INFO(this->get_logger(), "  Navaids: %zu VOR, %zu NDB, %zu LOC, %zu GS, %zu DME, %zu markers",
      world_->numVORs(), world_->numNDBs(), world_->numLOCs(),
      world_->numGSs(), world_->numDMEs(), world_->numMarkers());
    RCLCPP_INFO(this->get_logger(), "  Terrain: %s (%s)",
      terrain_dir.c_str(), task_->getTerrainModel().isAvailable() ? "loaded" : "NOT available");
    RCLCPP_INFO(this->get_logger(), "  AirportDB: %zu airports, %zu runways from %s (%s)",
      airport_db_ ? airport_db_->airportCount() : 0,
      airport_db_ ? airport_db_->runwayCount() : 0,
      apt_path.c_str(), apt_format.c_str());
    RCLCPP_INFO(this->get_logger(), "============================================");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State &) override
  {
    nav_signals_pub_->on_activate();

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

    RCLCPP_INFO(this->get_logger(), "navaid_sim activated (%.0f Hz)",
      this->get_parameter("update_rate_hz").as_double());
    publish_lifecycle_state("active");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) override
  {
    nav_signals_pub_->on_deactivate();
    heartbeat_timer_.reset();
    update_timer_.reset();
    RCLCPP_INFO(this->get_logger(), "navaid_sim deactivated");
    publish_lifecycle_state("inactive");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    heartbeat_pub_.reset();
    lifecycle_state_pub_.reset();
    nav_signals_pub_.reset();
    flight_model_sub_.reset();
    avionics_sub_.reset();
    failure_injection_sub_.reset();
    terrain_srv_.reset();
    search_airports_srv_.reset();
    get_runways_srv_.reset();
    search_navaids_srv_.reset();
    airport_db_.reset();
    failed_stations_.clear();
    task_.reset();
    model_.reset();
    world_.reset();
    magdec_.reset();
    RCLCPP_INFO(this->get_logger(), "navaid_sim cleaned up");
    publish_lifecycle_state("unconfigured");
    return CallbackReturn::SUCCESS;
  }

private:
  // Helper to resolve package share directory without ament_index_cpp dependency
  static std::string ament_index_cpp_get_share(const std::string & pkg)
  {
    // Check AMENT_PREFIX_PATH
    const char* env = std::getenv("AMENT_PREFIX_PATH");
    if (!env) throw std::runtime_error("no AMENT_PREFIX_PATH");
    std::string paths(env);
    size_t pos = 0;
    while (pos < paths.size()) {
      size_t end = paths.find(':', pos);
      if (end == std::string::npos) end = paths.size();
      std::string prefix = paths.substr(pos, end - pos);
      std::string share = prefix + "/share/" + pkg;
      struct stat st;
      if (stat(share.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return share;
      }
      pos = end + 1;
    }
    throw std::runtime_error("package not found: " + pkg);
  }

  static std::string to_upper(const std::string & s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
  }

  void on_failure_injection(const sim_msgs::msg::FailureInjection::SharedPtr & msg)
  {
    if (msg->method != "set_navaid_station_failed") {
      RCLCPP_WARN(this->get_logger(), "Unknown failure method: %s", msg->method.c_str());
      return;
    }

    // Parse station_id from params_json: {"station_id":"BUB"}
    std::string station_id;
    const auto & json = msg->params_json;
    auto key_pos = json.find("\"station_id\"");
    if (key_pos != std::string::npos) {
      auto colon_pos = json.find(':', key_pos + 12);
      if (colon_pos != std::string::npos) {
        auto q1 = json.find('"', colon_pos + 1);
        if (q1 != std::string::npos) {
          auto q2 = json.find('"', q1 + 1);
          if (q2 != std::string::npos) {
            station_id = json.substr(q1 + 1, q2 - q1 - 1);
          }
        }
      }
    }

    if (station_id.empty()) {
      RCLCPP_WARN(this->get_logger(), "set_navaid_station_failed: missing station_id in params_json");
      return;
    }

    station_id = to_upper(station_id);

    if (msg->active) {
      failed_stations_.insert(station_id);
      RCLCPP_INFO(this->get_logger(), "Navaid station FAILED: %s", station_id.c_str());
    } else {
      failed_stations_.erase(station_id);
      RCLCPP_INFO(this->get_logger(), "Navaid station RESTORED: %s", station_id.c_str());
    }
  }

  bool is_station_failed(const std::string & ident) const
  {
    if (failed_stations_.empty()) return false;
    return failed_stations_.count(to_upper(ident)) > 0;
  }

  void on_update()
  {
    if (!flight_model_received_ || !task_) return;

    // Feed aircraft position to the nav model
    float lat_deg = static_cast<float>(last_flight_model_state_.latitude_deg);
    float lon_deg = static_cast<float>(last_flight_model_state_.longitude_deg);
    float alt_ft  = static_cast<float>(last_flight_model_state_.altitude_msl_m * 3.28084);
    float hdg_deg = static_cast<float>(last_flight_model_state_.true_heading_rad / DEG_TO_RAD);
    model_->setPosition(lat_deg, lon_deg, alt_ft, hdg_deg);

    // Diagnostic: log nearest VOR on first position update
    if (!first_position_logged_) {
      first_position_logged_ = true;
      AS::VOR nearest(0,0,0,0,0,0,"","");
      double dist_nm = 0;
      LatLon pos(lat_deg, lon_deg);
      if (world_->findNearestVOR(pos, nearest, dist_nm)) {
        RCLCPP_INFO(this->get_logger(),
          "Aircraft at %.4f°N %.4f°E, %.0f ft — nearest VOR: %s (%s) %.2f MHz, %.1f nm away",
          lat_deg, lon_deg, alt_ft,
          nearest.mIdent.c_str(), nearest.mName.c_str(),
          nearest.mFrequency / 100.0, dist_nm);
      }
    }

    // Feed tuned frequencies from avionics controls
    // AvionicsControls: nav1_freq_mhz, nav2_freq_mhz are in MHz (float32), obs1/obs2 in degrees
    // Model expects: freq as int (MHz * 100), OBS as int degrees
    int nav1_int = 0, nav2_int = 0;
    if (last_avionics_.nav1_freq_mhz > 0.0f) {
      nav1_int = static_cast<int>(last_avionics_.nav1_freq_mhz * 100.0f + 0.5f);
      model_->setFrequency(1, nav1_int);
      model_->setOBS(1, static_cast<int>(last_avionics_.obs1 + 0.5f));
    }
    if (last_avionics_.nav2_freq_mhz > 0.0f) {
      nav2_int = static_cast<int>(last_avionics_.nav2_freq_mhz * 100.0f + 0.5f);
      model_->setFrequency(2, nav2_int);
      model_->setOBS(2, static_cast<int>(last_avionics_.obs2 + 0.5f));
    }
    // ADF frequency from avionics
    if (last_avionics_.adf1_freq_khz > 0.0f) {
      model_->setADF_Frequency(static_cast<int>(last_avionics_.adf1_freq_khz * 100.0f + 0.5f));
    }

    // Diagnostic: log when tuned frequency changes
    if (nav1_int != last_nav1_int_ || nav2_int != last_nav2_int_) {
      LatLon pos(lat_deg, lon_deg);
      // Check both VOR and LOC candidates for each radio
      auto nav1_vors = world_->getVOR(nav1_int, pos);
      auto nav1_locs = world_->getILS_LOC(nav1_int, pos);
      auto nav2_vors = world_->getVOR(nav2_int, pos);
      auto nav2_locs = world_->getILS_LOC(nav2_int, pos);
      RCLCPP_INFO(this->get_logger(),
        "Freq change — NAV1: %.2f MHz (int %d) → %zu VOR + %zu LOC | "
        "NAV2: %.2f MHz (int %d) → %zu VOR + %zu LOC",
        static_cast<double>(last_avionics_.nav1_freq_mhz), nav1_int, nav1_vors.size(), nav1_locs.size(),
        static_cast<double>(last_avionics_.nav2_freq_mhz), nav2_int, nav2_vors.size(), nav2_locs.size());
      for (auto& v : nav1_vors)
        RCLCPP_INFO(this->get_logger(), "  NAV1 VOR: %s (%s) %.1f nm",
          v.mIdent.c_str(), v.mName.c_str(), pos.get_distance_nm(v.mLatLon));
      for (auto& v : nav1_locs)
        RCLCPP_INFO(this->get_logger(), "  NAV1 LOC: %s %s RWY %s, bearing %.1f°, %.1f nm",
          v.mIdent.c_str(), v.mAirport.c_str(), v.mRunway.c_str(),
          v.mTrueBearing, pos.get_distance_nm(v.mLatLon));
      for (auto& v : nav2_vors)
        RCLCPP_INFO(this->get_logger(), "  NAV2 VOR: %s (%s) %.1f nm",
          v.mIdent.c_str(), v.mName.c_str(), pos.get_distance_nm(v.mLatLon));
      for (auto& v : nav2_locs)
        RCLCPP_INFO(this->get_logger(), "  NAV2 LOC: %s %s RWY %s, bearing %.1f°, %.1f nm",
          v.mIdent.c_str(), v.mAirport.c_str(), v.mRunway.c_str(),
          v.mTrueBearing, pos.get_distance_nm(v.mLatLon));
      last_nav1_int_ = nav1_int;
      last_nav2_int_ = nav2_int;
    }

    // Step all receivers
    task_->step();

    // Build and publish NavSignalTable
    auto out = sim_msgs::msg::NavSignalTable();
    out.header.stamp = this->now();

    // Tuned frequencies
    out.nav1_freq_mhz = last_avionics_.nav1_freq_mhz;
    out.nav2_freq_mhz = last_avionics_.nav2_freq_mhz;
    out.adf_freq_khz = model_->getADF_Frequency() / 100.0;

    // NAV1
    auto r1 = model_->getRadioResult(1);
    fill_nav_output(r1, out.nav1_valid, out.nav1_ident, out.nav1_type,
                    out.nav1_bearing_rad, out.nav1_radial_rad,
                    out.nav1_deviation_dots, out.nav1_distance_m,
                    out.nav1_signal_strength,
                    out.nav1_gs_valid, out.nav1_gs_deviation_dots);

    // Gate NAV1 if station is failed
    if (out.nav1_valid && is_station_failed(out.nav1_ident)) {
      out.nav1_valid = false;
      out.nav1_signal_strength = 0.0;
      out.nav1_gs_valid = false;
    }

    // NAV2
    auto r2 = model_->getRadioResult(2);
    fill_nav_output(r2, out.nav2_valid, out.nav2_ident, out.nav2_type,
                    out.nav2_bearing_rad, out.nav2_radial_rad,
                    out.nav2_deviation_dots, out.nav2_distance_m,
                    out.nav2_signal_strength,
                    out.nav2_gs_valid, out.nav2_gs_deviation_dots);

    // Gate NAV2 if station is failed
    if (out.nav2_valid && is_station_failed(out.nav2_ident)) {
      out.nav2_valid = false;
      out.nav2_signal_strength = 0.0;
      out.nav2_gs_valid = false;
    }

    // ADF
    out.adf_valid = r1.ndb_found != 0;
    out.adf_ident = r1.ndb_ident;
    out.adf_bearing_rad = r1.ndb_bearing * DEG_TO_RAD;
    out.adf_signal_strength = r1.ndb_found ? 1.0 : 0.0;

    // Gate ADF if NDB station is failed
    if (out.adf_valid && is_station_failed(out.adf_ident)) {
      out.adf_valid = false;
      out.adf_signal_strength = 0.0;
    }

    // Markers
    out.marker_outer = model_->getOuterMarker() != 0;
    out.marker_middle = model_->getMiddleMarker() != 0;
    out.marker_inner = model_->getInnerMarker() != 0;

    // DME (from NAV1 — primary DME channel)
    out.dme_valid = r1.dme_found != 0;
    out.dme_distance_m = r1.dme_distance_nm * NM_TO_M;
    out.dme_ground_speed_kts = 0.0;  // not computed by navaid_sim

    // Gate DME if NAV1 VOR station is failed (DME is co-located)
    if (out.dme_valid && is_station_failed(out.nav1_ident)) {
      out.dme_valid = false;
      out.dme_distance_m = 0.0;
    }

    nav_signals_pub_->publish(out);
  }

  static void fill_nav_output(const AS::RadioResult & r,
                               bool & valid, std::string & ident, uint8_t & type,
                               double & bearing_rad, double & radial_rad,
                               double & deviation_dots, double & distance_m,
                               double & signal_strength,
                               bool & gs_valid, double & gs_deviation_dots)
  {
    valid = r.vor_found != 0;
    ident = r.vor_ident;

    if (r.vor_localizer) {
      type = r.gs_found ? 3 : 4;  // 3=ILS, 4=LOC-only
    } else if (r.dme_found && r.vor_found) {
      type = 2;  // VORDME
    } else if (r.vor_found) {
      type = 1;  // VOR
    } else {
      type = 0;  // none
    }

    bearing_rad = r.vor_bearing * DEG_TO_RAD;
    // Radial = bearing FROM station (opposite of bearing TO station, adjusted)
    radial_rad = bearing_rad;  // vor_bearing is already the radial from the receiver logic
    // CDI deviation: VOR full scale = ±10°, LOC full scale = ±2.5°
    // Convert to dots (±2.5 dots full scale)
    if (r.vor_localizer) {
      // LOC: vor_deviation is in degrees, full scale ~2.5°
      deviation_dots = r.vor_deviation / 1.0;  // approx 1° per dot for LOC
    } else {
      // VOR: full scale ±10° = ±5 dots → 1 dot = 2°
      deviation_dots = r.vor_deviation / 2.0;
    }
    distance_m = r.vor_distance_m;
    signal_strength = valid ? 1.0 : 0.0;

    gs_valid = r.gs_found != 0;
    // GS deviation in degrees, full scale ±0.7° = ±2.5 dots → 1 dot = 0.28°
    gs_deviation_dots = r.gs_deviation / 0.28;
  }

  static sim_msgs::msg::Airport airport_to_msg(const Airport & apt)
  {
    sim_msgs::msg::Airport msg;
    msg.icao = apt.icao;
    msg.name = apt.name;
    msg.city = apt.city;
    msg.country = apt.country;
    msg.iata = apt.iata;
    msg.arp_lat_deg = apt.arp_lat_rad / DEG_TO_RAD;
    msg.arp_lon_deg = apt.arp_lon_rad / DEG_TO_RAD;
    msg.elevation_m = apt.elevation_m;
    msg.transition_altitude_ft = apt.transition_altitude_ft;
    for (auto & r : apt.runways) {
      sim_msgs::msg::Runway rmsg;
      rmsg.designator_end1 = r.end1.designator;
      rmsg.threshold_lat_deg_end1 = r.end1.threshold_lat_rad / DEG_TO_RAD;
      rmsg.threshold_lon_deg_end1 = r.end1.threshold_lon_rad / DEG_TO_RAD;
      rmsg.heading_deg_end1 = r.end1.heading_deg;
      rmsg.displaced_threshold_m_end1 = r.end1.displaced_threshold_m;
      rmsg.designator_end2 = r.end2.designator;
      rmsg.threshold_lat_deg_end2 = r.end2.threshold_lat_rad / DEG_TO_RAD;
      rmsg.threshold_lon_deg_end2 = r.end2.threshold_lon_rad / DEG_TO_RAD;
      rmsg.heading_deg_end2 = r.end2.heading_deg;
      rmsg.displaced_threshold_m_end2 = r.end2.displaced_threshold_m;
      rmsg.width_m = r.width_m;
      rmsg.length_m = r.length_m;
      rmsg.surface_type = r.surface_type;
      rmsg.elevation_m = r.end1.elevation_m;
      msg.runways.push_back(rmsg);
    }
    return msg;
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
  rclcpp_lifecycle::LifecyclePublisher<sim_msgs::msg::NavSignalTable>::SharedPtr nav_signals_pub_;

  // Subscriptions
  rclcpp::Subscription<sim_msgs::msg::FlightModelState>::SharedPtr flight_model_sub_;
  rclcpp::Subscription<sim_msgs::msg::AvionicsControls>::SharedPtr avionics_sub_;
  rclcpp::Subscription<sim_msgs::msg::FailureInjection>::SharedPtr failure_injection_sub_;
  rclcpp::Service<sim_msgs::srv::GetTerrainElevation>::SharedPtr terrain_srv_;
  rclcpp::Service<sim_msgs::srv::SearchAirports>::SharedPtr search_airports_srv_;
  rclcpp::Service<sim_msgs::srv::GetRunways>::SharedPtr get_runways_srv_;
  rclcpp::Service<sim_msgs::srv::SearchNavaids>::SharedPtr search_navaids_srv_;

  // Timers
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr auto_start_timer_;
  rclcpp::TimerBase::SharedPtr update_timer_;

  // Nav simulation core
  std::unique_ptr<AS::World> world_;
  std::unique_ptr<AS::Model> model_;
  std::unique_ptr<MagDec> magdec_;
  std::unique_ptr<NavSimTask> task_;
  std::unique_ptr<AirportDatabase> airport_db_;

  // Latest received state
  sim_msgs::msg::FlightModelState last_flight_model_state_;
  sim_msgs::msg::AvionicsControls last_avionics_;
  bool flight_model_received_{false};
  bool first_position_logged_{false};
  int last_nav1_int_{-1};
  int last_nav2_int_{-1};

  // Failed navaid stations (idents, uppercase)
  std::set<std::string> failed_stations_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<NavaidSimNode>();
  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}
