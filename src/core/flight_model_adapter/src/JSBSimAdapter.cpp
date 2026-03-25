#include "flight_model_adapter/JSBSimAdapter.hpp"
#include "flight_model_adapter/jsbsim/JSBSimElectricalWriteback.hpp"
#include "flight_model_adapter/jsbsim/JSBSimFuelWriteback.hpp"

#include <FGFDMExec.h>
#include <models/FGPropulsion.h>
#include <initialization/FGInitialCondition.h>
#include <simgear/misc/sg_path.hxx>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace flight_model_adapter
{

// Conversion constants
static constexpr double FT_TO_M   = 0.3048;
static constexpr double LBS_TO_KG = 0.453592;
static constexpr double LBF_TO_N  = 4.44822;
static constexpr double KTS_TO_MS = 0.514444;
static constexpr double PSF_TO_PA = 47.880258;
static constexpr double SLUG_TO_KG = 14.5939;
static constexpr double SLUGFT3_TO_KGM3 = 515.379;
static constexpr double R_TO_K = 5.0 / 9.0;  // Rankine to Kelvin factor
static constexpr double R_OFFSET = 273.15 * 9.0 / 5.0;  // Rankine offset for 0K
static constexpr double DEGR_TO_DEGC_SCALE = 5.0 / 9.0;
static constexpr double DEGR_TO_DEGC_OFFSET = 491.67;
static constexpr double DEGF_TO_DEGC_SCALE = 5.0 / 9.0;
static constexpr double DEGF_TO_DEGC_OFFSET = 32.0;
static constexpr double K_TO_DEGC_OFFSET = 273.15;
static constexpr double PSI_TO_PA = 6894.76;
static constexpr double INHG_TO_PA = 3386.39;
static constexpr double DEG_TO_RAD = M_PI / 180.0;
static constexpr double KG_TO_LBS = 1.0 / 0.453592;
static constexpr double AVGAS_DENSITY_KG_L = 0.72;
static constexpr double DEFAULT_DT_HZ = 50.0;

// ── Simple flat-JSON value extraction (no external library) ──────────────

static std::string extract_json_value(const std::string & json, const std::string & key)
{
  std::string search = "\"" + key + "\":";
  auto pos = json.find(search);
  if (pos == std::string::npos) return "";
  pos += search.size();
  while (pos < json.size() && json[pos] == ' ') pos++;
  if (pos >= json.size()) return "";
  if (json[pos] == '"') {
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
  }
  auto end = json.find_first_of(",}", pos);
  if (end == std::string::npos) return json.substr(pos);
  return json.substr(pos, end - pos);
}

static int json_int(const std::string & json, const std::string & key, int def = 0)
{
  auto v = extract_json_value(json, key);
  if (v.empty()) return def;
  return std::atoi(v.c_str());
}

static double json_double(const std::string & json, const std::string & key, double def = 0.0)
{
  auto v = extract_json_value(json, key);
  if (v.empty()) return def;
  return std::atof(v.c_str());
}

static bool json_bool(const std::string & json, const std::string & key, bool def = false)
{
  auto v = extract_json_value(json, key);
  if (v.empty()) return def;
  return v == "true";
}

// WGS-84 constants for ECEF conversion
static constexpr double WGS84_A = 6378137.0;
static constexpr double WGS84_E2 = 0.00669437999014;

/// Geodetic to ECEF conversion
static void geodetic_to_ecef(double lat_rad, double lon_rad, double alt_m,
                              double & x, double & y, double & z)
{
  double sin_lat = std::sin(lat_rad);
  double cos_lat = std::cos(lat_rad);
  double N = WGS84_A / std::sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);
  x = (N + alt_m) * cos_lat * std::cos(lon_rad);
  y = (N + alt_m) * cos_lat * std::sin(lon_rad);
  z = (N * (1.0 - WGS84_E2) + alt_m) * sin_lat;
}

/// NED to ECEF velocity rotation
static void ned_to_ecef_vel(double lat_rad, double lon_rad,
                             double vn, double ve, double vd,
                             double & vx, double & vy, double & vz)
{
  double sin_lat = std::sin(lat_rad);
  double cos_lat = std::cos(lat_rad);
  double sin_lon = std::sin(lon_rad);
  double cos_lon = std::cos(lon_rad);

  vx = -sin_lat * cos_lon * vn - sin_lon * ve - cos_lat * cos_lon * vd;
  vy = -sin_lat * sin_lon * vn + cos_lon * ve - cos_lat * sin_lon * vd;
  vz =  cos_lat * vn                          - sin_lat * vd;
}

JSBSimAdapter::JSBSimAdapter() = default;

JSBSimAdapter::~JSBSimAdapter() = default;

bool JSBSimAdapter::initialize(const std::string & aircraft_id,
                                const std::string & aircraft_path)
{
  aircraft_id_ = aircraft_id;

  try {
    exec_ = std::make_unique<JSBSim::FGFDMExec>();
    exec_->SetDebugLevel(0);

    // JSBSim expects separate root, aircraft, engine, systems dirs
    exec_->SetRootDir(SGPath(aircraft_path));
    exec_->SetAircraftPath(SGPath("aircraft"));
    exec_->SetEnginePath(SGPath("engine"));
    exec_->SetSystemsPath(SGPath("systems"));

    // Load the aircraft model
    // JSBSim C172P model dir is "c172p" under aircraft/
    std::string model_name;
    if (aircraft_id == "c172") {
      model_name = "c172p";
    } else {
      model_name = aircraft_id;
    }

    if (!exec_->LoadModel(model_name)) {
      std::cerr << "[JSBSimAdapter] Failed to load model: " << model_name << std::endl;
      return false;
    }

    internal_dt_ = exec_->GetDeltaT();
    if (internal_dt_ <= 0.0) {
      internal_dt_ = 1.0 / 120.0;
    }

    // Disable JSBSim console output
    exec_->SetDebugLevel(0);

    // Apply default initial conditions: on ground at EBBR, ready for takeoff.
    // sim_manager will override via apply_initial_conditions() once INIT→READY.
    auto fgic = exec_->GetIC();
    fgic->SetGeodLatitudeDegIC(50.9014);   // EBBR
    fgic->SetLongitudeDegIC(4.4844);
    fgic->SetAltitudeASLFtIC(100.0 / FT_TO_M);  // approximate — on-ground overrides
    fgic->SetPsiDegIC(254.0);          // Rwy 25L
    fgic->SetVcalibratedKtsIC(0.0);
    fgic->SetClimbRateFpsIC(0.0);
    exec_->SetPropertyValue("simulation/force-on-ground", 1.0);

    exec_->RunIC();

    // Ground config: engine running at idle, gear down, brakes set
    exec_->SetPropertyValue("gear/gear-cmd-norm", 1.0);
    exec_->SetPropertyValue("fcs/flap-cmd-norm", 0.0);
    exec_->SetPropertyValue("fcs/left-brake-cmd-norm", 1.0);
    exec_->SetPropertyValue("fcs/right-brake-cmd-norm", 1.0);
    exec_->SetPropertyValue("fcs/throttle-cmd-norm[0]", 0.1);
    exec_->SetPropertyValue("fcs/mixture-cmd-norm[0]", 1.0);
    // Use propulsion-level set-running (calls InitRunning which sets magnetos=3)
    exec_->SetPropertyValue("propulsion/set-running", 0);  // engine index 0

    initialized_ = true;
    std::cout << "[JSBSimAdapter] Loaded model '" << model_name
              << "' (dt=" << internal_dt_ << "s)" << std::endl;
    return true;

  } catch (const std::exception & e) {
    std::cerr << "[JSBSimAdapter] Exception during initialize: " << e.what() << std::endl;
    return false;
  } catch (const std::string & s) {
    std::cerr << "[JSBSimAdapter] JSBSim threw string during initialize: " << s << std::endl;
    return false;
  } catch (...) {
    std::cerr << "[JSBSimAdapter] Unknown exception during initialize" << std::endl;
    return false;
  }
}

void JSBSimAdapter::apply_initial_conditions(
  const sim_msgs::msg::InitialConditions & ic)
{
  if (!initialized_) { return; }

  try {
    auto fgic = exec_->GetIC();

    // Position — IC now carries degrees, JSBSim expects degrees
    fgic->SetGeodLatitudeDegIC(ic.latitude_deg);
    fgic->SetLongitudeDegIC(ic.longitude_deg);
    fgic->SetAltitudeASLFtIC(ic.altitude_msl_m / FT_TO_M);

    // Heading and attitude
    fgic->SetPsiDegIC(ic.heading_rad / DEG_TO_RAD);
    if (ic.bank_rad != 0.0)  fgic->SetPhiDegIC(ic.bank_rad / DEG_TO_RAD);
    if (ic.pitch_rad != 0.0) fgic->SetThetaDegIC(ic.pitch_rad / DEG_TO_RAD);

    // Configuration presets
    const std::string & config = ic.configuration;

    if (config == "airborne_clean") {
      // Set up in-flight: trimmed, gear up, flaps 0
      fgic->SetVcalibratedKtsIC(ic.airspeed_ms / KTS_TO_MS);
      fgic->SetClimbRateFpsIC(0.0);

      exec_->RunIC();
      exec_->SetPropertyValue("gear/gear-cmd-norm", 0.0);
      exec_->SetPropertyValue("fcs/flap-cmd-norm", 0.0);

      // Engine running at cruise power (propulsion-level InitRunning sets magnetos)
      exec_->SetPropertyValue("fcs/throttle-cmd-norm[0]", 0.65);
      exec_->SetPropertyValue("fcs/mixture-cmd-norm[0]", 1.0);
      exec_->SetPropertyValue("propulsion/set-running", 0);
    } else if (config == "ready_for_takeoff") {
      // On runway, engine running, brakes set
      fgic->SetVcalibratedKtsIC(0.0);
      fgic->SetClimbRateFpsIC(0.0);
      exec_->SetPropertyValue("simulation/force-on-ground", 1.0);

      exec_->RunIC();
      exec_->SetPropertyValue("gear/gear-cmd-norm", 1.0);
      exec_->SetPropertyValue("fcs/flap-cmd-norm", 0.0);
      exec_->SetPropertyValue("fcs/left-brake-cmd-norm", 1.0);
      exec_->SetPropertyValue("fcs/right-brake-cmd-norm", 1.0);

      // Start engine (propulsion-level InitRunning sets magnetos)
      exec_->SetPropertyValue("fcs/throttle-cmd-norm[0]", 0.1);
      exec_->SetPropertyValue("fcs/mixture-cmd-norm[0]", 1.0);
      exec_->SetPropertyValue("propulsion/set-running", 0);
    } else {
      // cold_and_dark or unknown — everything off
      fgic->SetVcalibratedKtsIC(0.0);
      fgic->SetClimbRateFpsIC(0.0);

      exec_->RunIC();
      exec_->SetPropertyValue("gear/gear-cmd-norm", 1.0);
      exec_->SetPropertyValue("fcs/throttle-cmd-norm[0]", 0.0);
      exec_->SetPropertyValue("fcs/mixture-cmd-norm[0]", 0.0);
      exec_->SetPropertyValue("propulsion/engine[0]/set-running", 0);
    }

  } catch (const std::exception & e) {
    std::cerr << "[JSBSimAdapter] Exception in apply_initial_conditions: "
              << e.what() << std::endl;
  } catch (const std::string & s) {
    std::cerr << "[JSBSimAdapter] JSBSim threw string in apply_initial_conditions: "
              << s << std::endl;
  } catch (...) {
    std::cerr << "[JSBSimAdapter] Unknown exception in apply_initial_conditions" << std::endl;
  }
}

bool JSBSimAdapter::step(double dt_sec)
{
  if (!initialized_ || !exec_) { return false; }

  try {
    // Run multiple JSBSim sub-steps to cover the requested dt
    int steps = static_cast<int>(std::round(dt_sec / internal_dt_));
    if (steps < 1) { steps = 1; }

    for (int i = 0; i < steps; ++i) {
      if (!exec_->Run()) {
        return false;
      }
    }

    // Apply active fuel drain failures
    if (!active_drains_.empty()) {
      for (const auto & [tank_idx, rate_lph] : active_drains_) {
        double drain_kg = (rate_lph / 3600.0) * (1.0 / DEFAULT_DT_HZ) * AVGAS_DENSITY_KG_L;
        std::string prop = "propulsion/tank[" + std::to_string(tank_idx) + "]/contents-lbs";
        double current_lbs = exec_->GetPropertyValue(prop);
        double new_lbs = std::max(0.0, current_lbs - drain_kg * KG_TO_LBS);
        exec_->SetPropertyValue(prop, new_lbs);
      }
    }

    return true;

  } catch (const std::exception & e) {
    std::cerr << "[JSBSimAdapter] Exception in step(): " << e.what() << std::endl;
    return false;
  } catch (const std::string & s) {
    std::cerr << "[JSBSimAdapter] JSBSim threw string in step(): " << s << std::endl;
    return false;
  } catch (...) {
    std::cerr << "[JSBSimAdapter] Unknown exception in step()" << std::endl;
    return false;
  }
}

void JSBSimAdapter::apply_failure(const std::string & method,
                                   const std::string & params_json,
                                   bool active)
{
  if (!initialized_ || !exec_) return;

  try {

  if (method == "set_engine_running") {
    int idx = json_int(params_json, "engine_index");
    bool val = json_bool(params_json, "value");
    std::string prop = "propulsion/engine[" + std::to_string(idx) + "]/set-running";
    if (active) {
      exec_->SetPropertyValue(prop, val ? 1.0 : 0.0);
    } else {
      exec_->SetPropertyValue(prop, 1.0);  // restore running
    }
  } else if (method == "set_engine_thrust_scalar") {
    int idx = json_int(params_json, "engine_index");
    double val = json_double(params_json, "value", 1.0);
    std::string prop = "propulsion/engine[" + std::to_string(idx) + "]/thrust-scalar";
    exec_->SetPropertyValue(prop, active ? val : 1.0);
  } else if (method == "set_engine_oil_pressure") {
    int idx = json_int(params_json, "engine_index");
    double psi = json_double(params_json, "value_psi", 60.0);
    std::string prop = "propulsion/engine[" + std::to_string(idx) + "]/oil-pressure-psi";
    exec_->SetPropertyValue(prop, active ? psi : 60.0);
  } else if (method == "set_engine_fire") {
    int idx = json_int(params_json, "engine_index");
    bool val = json_bool(params_json, "value");
    std::string prop = "propulsion/engine[" + std::to_string(idx) + "]/fire-now";
    exec_->SetPropertyValue(prop, (active && val) ? 1.0 : 0.0);
  } else if (method == "set_fuel_tank_drain") {
    int idx = json_int(params_json, "tank_index");
    float rate = static_cast<float>(json_double(params_json, "rate_lph", 20.0));
    if (active) {
      active_drains_[idx] = rate;
    } else {
      active_drains_.erase(idx);
    }
  } else if (method == "set_pitot_failed") {
    int idx = json_int(params_json, "index");
    std::string prop = "systems/pitot[" + std::to_string(idx) + "]/serviceable";
    exec_->SetPropertyValue(prop, active ? 0.0 : 1.0);
  } else if (method == "set_gear_unsafe_indication") {
    int idx = json_int(params_json, "gear_index");
    std::string prop = "gear/unit[" + std::to_string(idx) + "]/wow";
    exec_->SetPropertyValue(prop, active ? 0.0 : -1.0);
  } else if (method == "set_gear_unable_to_extend") {
    int idx = json_int(params_json, "gear_index");
    std::string prop = "gear/unit[" + std::to_string(idx) + "]/pos-norm";
    exec_->SetPropertyValue(prop, active ? 0.0 : 1.0);
  } else if (method == "set_tail_rotor_failed") {
    exec_->SetPropertyValue("systems/tail-rotor/serviceable", active ? 0.0 : 1.0);
  } else {
    std::cerr << "[JSBSimAdapter] Unknown failure method: " << method << std::endl;
  }

  } catch (const std::exception & e) {
    std::cerr << "[JSBSimAdapter] Exception in apply_failure(" << method << "): "
              << e.what() << std::endl;
  } catch (const std::string & s) {
    std::cerr << "[JSBSimAdapter] JSBSim threw string in apply_failure(" << method << "): "
              << s << std::endl;
  } catch (...) {
    std::cerr << "[JSBSimAdapter] Unknown exception in apply_failure(" << method << ")"
              << std::endl;
  }
}

sim_msgs::msg::FlightModelState JSBSimAdapter::get_state() const
{
  sim_msgs::msg::FlightModelState state;

  if (!initialized_ || !exec_) { return state; }

  try {

  // --- 1. HEADER & METADATA ---
  state.sim_time_sec = exec_->GetPropertyValue("simulation/sim-time-sec");
  state.flight_model_name = "jsbsim";
  state.aircraft_id = aircraft_id_;
  state.is_helicopter = false;
  state.has_multi_engine = false;
  state.has_retractable_gear = false;  // C172 has fixed gear
  state.is_frozen = false;

  // --- 2. POSITION ---
  double lat_rad = exec_->GetPropertyValue("position/lat-geod-rad");
  double lon_rad = exec_->GetPropertyValue("position/long-gc-rad");
  double alt_msl_ft = exec_->GetPropertyValue("position/h-sl-ft");
  double alt_agl_ft = exec_->GetPropertyValue("position/h-agl-ft");

  state.latitude_deg = lat_rad * (180.0 / M_PI);
  state.longitude_deg = lon_rad * (180.0 / M_PI);
  state.altitude_msl_m = alt_msl_ft * FT_TO_M;
  state.altitude_agl_m = alt_agl_ft * FT_TO_M;

  geodetic_to_ecef(lat_rad, lon_rad, state.altitude_msl_m,
                   state.ecef_x_m, state.ecef_y_m, state.ecef_z_m);

  // --- 3. ORIENTATION ---
  // JSBSim quaternion: ECI-to-Body. We need NED-to-Body.
  // Use Euler angles to construct NED-to-Body quaternion.
  double phi   = exec_->GetPropertyValue("attitude/phi-rad");
  double theta = exec_->GetPropertyValue("attitude/theta-rad");
  double psi   = exec_->GetPropertyValue("attitude/psi-rad");

  state.roll_rad = phi;
  state.pitch_rad = theta;
  state.true_heading_rad = psi;

  // Construct quaternion from Euler (ZYX: psi, theta, phi)
  double c_psi2   = std::cos(psi * 0.5);
  double s_psi2   = std::sin(psi * 0.5);
  double c_theta2 = std::cos(theta * 0.5);
  double s_theta2 = std::sin(theta * 0.5);
  double c_phi2   = std::cos(phi * 0.5);
  double s_phi2   = std::sin(phi * 0.5);

  state.q_w = c_psi2 * c_theta2 * c_phi2 + s_psi2 * s_theta2 * s_phi2;
  state.q_x = c_psi2 * c_theta2 * s_phi2 - s_psi2 * s_theta2 * c_phi2;
  state.q_y = c_psi2 * s_theta2 * c_phi2 + s_psi2 * c_theta2 * s_phi2;
  state.q_z = s_psi2 * c_theta2 * c_phi2 - c_psi2 * s_theta2 * s_phi2;

  // Magnetic heading — approximate with a fixed declination (proper model deferred)
  state.magnetic_heading_rad = psi;  // TODO: apply magnetic declination

  // --- 4. VELOCITY ---
  // NED
  double vn_fps = exec_->GetPropertyValue("velocities/v-north-fps");
  double ve_fps = exec_->GetPropertyValue("velocities/v-east-fps");
  double vd_fps = exec_->GetPropertyValue("velocities/v-down-fps");
  state.vel_north_ms = vn_fps * FT_TO_M;
  state.vel_east_ms  = ve_fps * FT_TO_M;
  state.vel_down_ms  = vd_fps * FT_TO_M;

  // Body
  state.vel_u_ms = exec_->GetPropertyValue("velocities/u-fps") * FT_TO_M;
  state.vel_v_ms = exec_->GetPropertyValue("velocities/v-fps") * FT_TO_M;
  state.vel_w_ms = exec_->GetPropertyValue("velocities/w-fps") * FT_TO_M;

  // ECEF
  ned_to_ecef_vel(lat_rad, lon_rad,
                  state.vel_north_ms, state.vel_east_ms, state.vel_down_ms,
                  state.vel_ecef_x_ms, state.vel_ecef_y_ms, state.vel_ecef_z_ms);

  // --- 5. ANGULAR RATES ---
  state.roll_rate_rads  = exec_->GetPropertyValue("velocities/p-rad_sec");
  state.pitch_rate_rads = exec_->GetPropertyValue("velocities/q-rad_sec");
  state.yaw_rate_rads   = exec_->GetPropertyValue("velocities/r-rad_sec");

  // --- 6. ACCELERATION ---
  state.accel_x_ms2 = exec_->GetPropertyValue("accelerations/a-pilot-x-ft_sec2") * FT_TO_M;
  state.accel_y_ms2 = exec_->GetPropertyValue("accelerations/a-pilot-y-ft_sec2") * FT_TO_M;
  state.accel_z_ms2 = exec_->GetPropertyValue("accelerations/a-pilot-z-ft_sec2") * FT_TO_M;

  state.load_factor_x = exec_->GetPropertyValue("accelerations/Nx");
  state.load_factor_y = exec_->GetPropertyValue("accelerations/Ny");
  state.load_factor_z = exec_->GetPropertyValue("accelerations/Nz");

  // --- 7. AIR DATA ---
  state.ias_ms   = exec_->GetPropertyValue("velocities/vc-kts") * KTS_TO_MS;
  state.cas_ms   = state.ias_ms;  // JSBSim vc-kts is CAS; treat same for now
  state.tas_ms   = exec_->GetPropertyValue("velocities/vt-fps") * FT_TO_M;
  state.eas_ms   = exec_->GetPropertyValue("velocities/ve-kts") * KTS_TO_MS;

  state.ground_speed_ms = std::sqrt(
    state.vel_north_ms * state.vel_north_ms +
    state.vel_east_ms * state.vel_east_ms);

  state.ground_track_rad = std::atan2(state.vel_east_ms, state.vel_north_ms);
  if (state.ground_track_rad < 0.0) { state.ground_track_rad += 2.0 * M_PI; }

  state.vertical_speed_ms = -state.vel_down_ms;  // positive = climbing

  state.mach_number = exec_->GetPropertyValue("velocities/mach");

  state.alpha_rad = exec_->GetPropertyValue("aero/alpha-rad");
  state.beta_rad  = exec_->GetPropertyValue("aero/beta-rad");
  state.alpha_dot_rads = exec_->GetPropertyValue("aero/alphadot-rad_sec");
  state.beta_dot_rads  = exec_->GetPropertyValue("aero/betadot-rad_sec");

  state.baro_altitude_m = state.altitude_msl_m;  // simplified (no baro correction)
  state.radar_altitude_m = state.altitude_agl_m;

  state.dynamic_pressure_pa = exec_->GetPropertyValue("aero/qbar-psf") * PSF_TO_PA;
  state.static_pressure_pa  = exec_->GetPropertyValue("atmosphere/P-psf") * PSF_TO_PA;

  // Density: JSBSim atmosphere/rho-slugs_ft3
  state.air_density_kgm3 = exec_->GetPropertyValue("atmosphere/rho-slugs_ft3") * SLUGFT3_TO_KGM3;

  // Temperature: JSBSim in Rankine
  double temp_r = exec_->GetPropertyValue("atmosphere/T-R");
  state.temperature_k = temp_r * R_TO_K;  // Rankine to Kelvin: K = R * 5/9

  state.wind_speed_ms = exec_->GetPropertyValue("atmosphere/total-wind-north-fps") * FT_TO_M;
  state.wind_direction_rad = 0.0;  // TODO: compute from wind components
  state.wind_vertical_ms = 0.0;

  // --- 8. ENGINES ---
  state.engine_count = 1;
  state.throttle_pct[0] = static_cast<float>(
    exec_->GetPropertyValue("fcs/throttle-pos-norm[0]") * 100.0);
  state.fuel_flow_kgs[0] = static_cast<float>(
    exec_->GetPropertyValue("propulsion/engine[0]/fuel-flow-rate-pps") * LBS_TO_KG);
  state.n1_pct[0] = 0.0f;  // piston engine — no N1
  state.n2_pct[0] = 0.0f;
  state.n1_rpm[0] = static_cast<float>(
    exec_->GetPropertyValue("propulsion/engine[0]/propeller-rpm"));
  state.n2_rpm[0] = 0.0f;
  state.power_turbine_rpm[0] = 0.0f;
  state.power_turbine_pct[0] = 0.0f;
  state.torque_nm[0] = 0.0f;
  state.torque_pct[0] = 0.0f;
  state.iat_k[0] = state.temperature_k;

  // EGT: FGPiston reports in °F (not °R) → convert to °C
  double egt_degf = exec_->GetPropertyValue("propulsion/engine[0]/egt-degF");
  double egt_degc = (egt_degf - DEGF_TO_DEGC_OFFSET) * DEGF_TO_DEGC_SCALE;
  state.engine_egt_degc[0] = static_cast<float>(egt_degc);
  state.itt_k[0] = static_cast<float>(egt_degc + K_TO_DEGC_OFFSET);  // SI backfill (°C → K)

  // Oil pressure: FGPiston reports in PSI
  double oil_psi = exec_->GetPropertyValue("propulsion/engine[0]/oil-pressure-psi");
  state.engine_oil_pressure_psi[0] = static_cast<float>(oil_psi);
  state.oil_pressure_pa[0] = static_cast<float>(oil_psi * PSI_TO_PA);  // SI backfill

  // Oil temperature: FGPiston reports in °F → convert to °C
  double oil_degf = exec_->GetPropertyValue("propulsion/engine[0]/oil-temperature-degF");
  double oil_degc = (oil_degf - DEGF_TO_DEGC_OFFSET) * DEGF_TO_DEGC_SCALE;
  state.engine_oil_temp_degc[0] = static_cast<float>(oil_degc);
  state.oil_temperature_k[0] = static_cast<float>(oil_degc + K_TO_DEGC_OFFSET);  // SI backfill

  // CHT: FGPiston reports in °F → convert to °C
  double cht_degf = exec_->GetPropertyValue("propulsion/engine[0]/cht-degF");
  state.engine_cht_degc[0] = static_cast<float>((cht_degf - DEGF_TO_DEGC_OFFSET) * DEGF_TO_DEGC_SCALE);

  // Manifold pressure: FGPiston property is "map-inhg"
  double map_inhg = exec_->GetPropertyValue("propulsion/engine[0]/map-inhg");
  state.engine_manifold_pressure_inhg[0] = static_cast<float>(map_inhg);

  // Engine running status
  bool running = exec_->GetPropertyValue("propulsion/engine[0]/set-running") > 0.5;
  state.engine_status_flags[0] = running ? 0x01 : 0x00;

  // Zero remaining engines
  for (int i = 1; i < 4; ++i) {
    state.throttle_pct[i] = 0.0f;
    state.fuel_flow_kgs[i] = 0.0f;
    state.n1_pct[i] = 0.0f;
    state.n2_pct[i] = 0.0f;
    state.n1_rpm[i] = 0.0f;
    state.n2_rpm[i] = 0.0f;
    state.power_turbine_rpm[i] = 0.0f;
    state.power_turbine_pct[i] = 0.0f;
    state.torque_nm[i] = 0.0f;
    state.torque_pct[i] = 0.0f;
    state.iat_k[i] = 0.0f;
    state.itt_k[i] = 0.0f;
    state.oil_pressure_pa[i] = 0.0f;
    state.oil_temperature_k[i] = 0.0f;
    state.engine_egt_degc[i] = 0.0f;
    state.engine_oil_pressure_psi[i] = 0.0f;
    state.engine_oil_temp_degc[i] = 0.0f;
    state.engine_cht_degc[i] = 0.0f;
    state.engine_manifold_pressure_inhg[i] = 0.0f;
    state.engine_status_flags[i] = 0;
  }

  // --- 9. FUEL & MASS ---
  auto propulsion = exec_->GetPropulsion();
  int num_tanks = propulsion ? static_cast<int>(propulsion->GetNumTanks()) : 0;
  if (num_tanks > 8) { num_tanks = 8; }  // FlightModelState.fuel_tank_kg is [8]
  state.fuel_tank_count = static_cast<uint8_t>(num_tanks);

  double total_contents_lbs = 0.0;
  double total_capacity_lbs = 0.0;
  for (int i = 0; i < num_tanks; ++i) {
    std::string base = "propulsion/tank[" + std::to_string(i) + "]/";
    double contents_lbs = exec_->GetPropertyValue(base + "contents-lbs");
    double capacity_lbs = exec_->GetPropertyValue(base + "capacity-lbs");
    state.fuel_tank_kg[i] = static_cast<float>(contents_lbs * LBS_TO_KG);
    state.fuel_tank_capacity_kg[i] = static_cast<float>(capacity_lbs * LBS_TO_KG);
    total_contents_lbs += contents_lbs;
    total_capacity_lbs += capacity_lbs;
  }
  for (int i = num_tanks; i < 8; ++i) {
    state.fuel_tank_kg[i] = 0.0f;
    state.fuel_tank_capacity_kg[i] = 0.0f;
  }

  state.fuel_total_kg = static_cast<float>(total_contents_lbs * LBS_TO_KG);
  state.fuel_total_pct = (total_capacity_lbs > 0.0)
    ? static_cast<float>(total_contents_lbs / total_capacity_lbs)
    : 0.0f;

  state.total_mass_kg = static_cast<float>(
    exec_->GetPropertyValue("inertia/weight-lbs") * LBS_TO_KG);

  // --- 10. LANDING GEAR ---
  // C172 has 3 fixed gear: [0]=nose, [1]=left main, [2]=right main
  state.gear_count = 3;
  for (int i = 0; i < 3; ++i) {
    state.gear_position_pct[i] = 1.0f;  // always extended
    std::string wow_prop = "gear/unit[" + std::to_string(i) + "]/WOW";
    state.gear_on_ground[i] = exec_->GetPropertyValue(wow_prop) > 0.5;
    state.gear_status[i] = 1;  // always down
    state.wheel_angle_deg[i] = 0.0f;
  }
  for (int i = 3; i < 5; ++i) {
    state.gear_position_pct[i] = 0.0f;
    state.gear_on_ground[i] = false;
    state.gear_status[i] = 0;
    state.wheel_angle_deg[i] = 0.0f;
  }

  // --- 11. FLIGHT STATE ---
  state.on_ground = state.gear_on_ground[0] || state.gear_on_ground[1] || state.gear_on_ground[2];
  state.crashed = false;
  state.flight_state = state.on_ground ? 0 : 4;  // 0=parked, 4=cruise (simplified)

  // --- 12. AUTOPILOT (no autopilot — all zeroed) ---
  state.autopilot_engaged = false;
  state.flight_director_on = false;
  state.ap_pitch_mode = 0;
  state.ap_roll_mode = 0;
  state.ap_yaw_mode = 0;
  state.ap_pitch_arm_mode = 0;
  state.ap_roll_arm_mode = 0;
  state.cmd_pitch_rad = 0.0f;
  state.cmd_roll_rad = 0.0f;
  state.cmd_heading_rad = 0.0f;
  state.cmd_vertical_speed_ms = 0.0f;

  // --- 13. CONTROL SURFACES ---
  state.aileron_left_deg = static_cast<float>(
    exec_->GetPropertyValue("fcs/left-aileron-pos-deg"));
  state.aileron_right_deg = static_cast<float>(
    exec_->GetPropertyValue("fcs/right-aileron-pos-deg"));
  state.elevator_deg = static_cast<float>(
    exec_->GetPropertyValue("fcs/elevator-pos-deg"));
  state.rudder_deg = static_cast<float>(
    exec_->GetPropertyValue("fcs/rudder-pos-deg"));
  state.flap_pct = static_cast<float>(
    exec_->GetPropertyValue("fcs/flap-pos-norm") * 100.0);
  state.speed_brake_pct = 0.0f;
  state.aileron_trim_deg = static_cast<float>(
    exec_->GetPropertyValue("fcs/aileron-trim-cmd-norm") * 10.0);
  state.elevator_trim_deg = static_cast<float>(
    exec_->GetPropertyValue("fcs/pitch-trim-cmd-norm") * 10.0);
  state.rudder_trim_deg = static_cast<float>(
    exec_->GetPropertyValue("fcs/yaw-trim-cmd-norm") * 10.0);

  // --- 14. ROTOR (zeroed for fixed-wing) ---
  state.main_shaft_torque_nm = 0.0f;
  state.main_shaft_torque_pct = 0.0f;
  state.rotor_rpm = {0.0f, 0.0f};
  state.rotor_accel_rads2 = {0.0f, 0.0f};
  state.cyclic_lateral_deg = 0.0f;
  state.cyclic_longitudinal_deg = 0.0f;
  state.pedal_trim_deg = 0.0f;
  state.collective_deg = 0.0f;
  state.collective_pct = 0.0f;
  state.sas_engaged = false;
  state.collective_control_engaged = false;
  state.ap_collective_mode = 0;

  // --- 15. INTEROP ---
  state.sim_clock = state.sim_time_sec;

  } catch (const std::exception & e) {
    std::cerr << "[JSBSimAdapter] Exception in get_state(): " << e.what() << std::endl;
  } catch (const std::string & s) {
    std::cerr << "[JSBSimAdapter] JSBSim threw string in get_state(): " << s << std::endl;
  } catch (...) {
    std::cerr << "[JSBSimAdapter] Unknown exception in get_state()" << std::endl;
  }

  return state;
}

void JSBSimAdapter::apply_engine_commands(const sim_msgs::msg::EngineCommands & /*cmd*/)
{
  // No-op for current aircraft (piston C172).
  // Turboprop/FADEC write-back will be implemented when those aircraft are added.
}

FlightModelCapabilities JSBSimAdapter::get_capabilities() const
{
  // JSBSim computes physics (aero, prop, mass, ground reactions) but
  // does not model cockpit-level systems (magneto switches, fuel selectors,
  // electrical buses, etc.) — our systems nodes handle that layer.
  FlightModelCapabilities caps;
  // All fields default to FDM_NATIVE.

  // Electrical: JSBSim has no bus model — we run the solver, write bus voltages
  // back so JSBSim starter motor torque sees the correct supply.
  caps.electrical = CapabilityMode::EXTERNAL_COUPLED;

  // Fuel quantities: JSBSim tracks tank contents natively but our solver may
  // need to override (selector logic, crossfeed). Writeback keeps them in sync.
  caps.fuel_quantities = CapabilityMode::EXTERNAL_COUPLED;

  // Fuel pump pressure: our solver computes pump delivery pressure and writes
  // it back so JSBSim engine model sees correct fuel pressure.
  caps.fuel_pump_pressure = CapabilityMode::EXTERNAL_COUPLED;

  return caps;
}

void JSBSimAdapter::set_property(const std::string & name, double value)
{
  if (!initialized_ || !exec_) return;
  try {
    exec_->SetPropertyValue(name, value);
  } catch (const std::string & s) {
    std::cerr << "[JSBSimAdapter] set_property(" << name << "): " << s << std::endl;
  } catch (...) {}
}

double JSBSimAdapter::get_property(const std::string & name) const
{
  if (!initialized_ || !exec_) return 0.0;
  try {
    return exec_->GetPropertyValue(name);
  } catch (...) {
    return 0.0;
  }
}

void JSBSimAdapter::write_back_electrical(const sim_msgs::msg::ElectricalState & state)
{
  try {
    jsbsim_writeback::write_electrical(exec_.get(), state);
  } catch (const std::exception & e) {
    std::cerr << "[JSBSimAdapter] Exception in write_back_electrical: " << e.what() << std::endl;
  } catch (const std::string & s) {
    std::cerr << "[JSBSimAdapter] JSBSim error in write_back_electrical: " << s << std::endl;
  } catch (...) {
    std::cerr << "[JSBSimAdapter] Unknown exception in write_back_electrical" << std::endl;
  }
}

void JSBSimAdapter::write_back_fuel(const sim_msgs::msg::FuelState & state)
{
  try {
    jsbsim_writeback::write_fuel(exec_.get(), state);
  } catch (const std::exception & e) {
    std::cerr << "[JSBSimAdapter] Exception in write_back_fuel: " << e.what() << std::endl;
  } catch (const std::string & s) {
    std::cerr << "[JSBSimAdapter] JSBSim error in write_back_fuel: " << s << std::endl;
  } catch (...) {
    std::cerr << "[JSBSimAdapter] Unknown exception in write_back_fuel" << std::endl;
  }
}

void JSBSimAdapter::refine_terrain_altitude(double alt_msl_m, double terrain_elev_m)
{
    if (!initialized_) return;

    // Set terrain elevation first (safe — only touches ground callback)
    double terrain_ft = terrain_elev_m / FT_TO_M;
    exec_->SetPropertyValue("position/terrain-elevation-asl-ft", terrain_ft);

    // Save cockpit state that RunIC() will reset
    double throttle_0 = exec_->GetPropertyValue("fcs/throttle-cmd-norm[0]");
    double mixture_0  = exec_->GetPropertyValue("fcs/mixture-cmd-norm[0]");
    double gear_cmd   = exec_->GetPropertyValue("gear/gear-cmd-norm");
    double flap_cmd   = exec_->GetPropertyValue("fcs/flap-cmd-norm");
    double brake_l    = exec_->GetPropertyValue("fcs/left-brake-cmd-norm");
    double brake_r    = exec_->GetPropertyValue("fcs/right-brake-cmd-norm");
    bool   eng_run    = exec_->GetPropertyValue("propulsion/engine[0]/set-running") > 0.5;

    // Update altitude in IC object and re-run.
    // The IC object still holds the correct geodetic lat/lon/heading from
    // the last apply_initial_conditions() call (which uses SetGeodLatitudeDegIC).
    // This avoids the FGLocation cache corruption caused by
    // SetPropertyValue("position/h-sl-ft") → SetAltitudeASL → SetRadius,
    // which introduced a geocentric/geodetic roundtrip error (~0.17° at 51°N).
    auto fgic = exec_->GetIC();
    fgic->SetAltitudeASLFtIC(alt_msl_m / FT_TO_M);
    exec_->RunIC();

    // Re-set terrain after RunIC (RunIC may reset it)
    exec_->SetPropertyValue("position/terrain-elevation-asl-ft", terrain_ft);

    // Restore cockpit state
    exec_->SetPropertyValue("fcs/throttle-cmd-norm[0]", throttle_0);
    exec_->SetPropertyValue("fcs/mixture-cmd-norm[0]", mixture_0);
    exec_->SetPropertyValue("gear/gear-cmd-norm", gear_cmd);
    exec_->SetPropertyValue("fcs/flap-cmd-norm", flap_cmd);
    exec_->SetPropertyValue("fcs/left-brake-cmd-norm", brake_l);
    exec_->SetPropertyValue("fcs/right-brake-cmd-norm", brake_r);
    if (eng_run) {
        exec_->SetPropertyValue("propulsion/set-running", 0);
    }
}

}  // namespace flight_model_adapter
