#include <sim_interfaces/i_air_data_model.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <yaml-cpp/yaml.h>

#include <string>
#include <vector>
#include <cmath>
#include <stdexcept>
#include <chrono>

namespace aircraft_c172
{

class AirDataModel : public sim_interfaces::IAirDataModel
{
public:
  void configure(const std::string & yaml_path) override
  {
    YAML::Node root = YAML::LoadFile(yaml_path);
    auto air_data = root["air_data"];
    if (!air_data) {
      throw std::runtime_error("air_data.yaml: missing 'air_data' key");
    }

    auto systems_node = air_data["systems"];
    if (!systems_node || !systems_node.IsSequence() || systems_node.size() == 0) {
      throw std::runtime_error("air_data.yaml: 'air_data.systems' must be a non-empty sequence");
    }

    // C172 has a single pitot-static system — only parse the first entry
    auto sys = systems_node[0];
    system_.name = sys["name"] ? sys["name"].as<std::string>() : "primary";

    auto pitot = sys["pitot"];
    if (pitot) {
      heat_load_name_  = pitot["heat_load"] ? pitot["heat_load"].as<std::string>() : "pitot_heat";
      ice_delay_s_     = pitot["ice_delay_s"] ? pitot["ice_delay_s"].as<float>() : 45.0f;
      ice_clear_rate_  = pitot["ice_clear_rate"] ? pitot["ice_clear_rate"].as<float>() : 2.0f;
    }

    auto static_node = sys["static"];
    if (static_node) {
      alternate_switch_id_  = static_node["alternate_switch"]
        ? static_node["alternate_switch"].as<std::string>() : "sw_alt_static";
      alternate_offset_pa_  = static_node["alternate_offset_pa"]
        ? static_node["alternate_offset_pa"].as<float>() : -30.0f;
    }

    auto turb = sys["turbulence"];
    if (turb) {
      pitot_noise_gain_ = turb["pitot_noise_gain"]
        ? turb["pitot_noise_gain"].as<float>() : 0.15f;
    }

    vsi_lag_s_ = sys["vsi_lag_s"] ? sys["vsi_lag_s"].as<float>() : 1.5f;

    // Seed RNG from wall clock
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    rng_state_ = static_cast<uint64_t>(now) ^ 0xdeadbeefcafeULL;

    reset();
  }

  void update(double dt_sec, const sim_interfaces::AirDataInputs & inputs) override
  {
    if (dt_sec <= 0.0) return;

    // ── Physical constants ───────────────────────────────────────────────────
    constexpr double P_sl    = 101325.0;   // sea-level standard pressure (Pa)
    constexpr double rho_sl  = 1.225;      // sea-level standard density (kg/m³)
    constexpr double T_sl    = 288.15;     // sea-level standard temperature (K)
    constexpr double L       = 0.0065;     // ISA lapse rate (K/m)
    constexpr double g       = 9.80665;    // gravitational acceleration (m/s²)
    constexpr double R       = 287.05287;  // specific gas constant for dry air
    // constexpr double gamma_air = 1.4;   // ratio of specific heats (used in Mach)

    // ── Pitot pressure ───────────────────────────────────────────────────────
    double eff_density = (inputs.density_kgm3 > 0.0) ? inputs.density_kgm3 : rho_sl;
    double dynamic_pressure = 0.5 * eff_density * inputs.tas_ms * inputs.tas_ms;
    double pitot_pressure = inputs.static_pressure_pa + dynamic_pressure;

    // Turbulence noise on pitot (band-limited, scaled by TAS × turbulence_intensity)
    if (inputs.turbulence_intensity > 0.0 && inputs.tas_ms > 1.0) {
      double raw_noise = generate_noise_sample();
      noise_state_ += (raw_noise - noise_state_) * dt_sec / 0.5;  // 0.5s low-pass
      double noise = noise_state_ *
        inputs.turbulence_intensity * inputs.tas_ms *
        static_cast<double>(pitot_noise_gain_);
      pitot_pressure += noise;
    } else {
      noise_state_ = 0.0;
    }

    // ── Pitot icing model ────────────────────────────────────────────────────
    const bool heat_on = !inputs.pitot_heat_powered.empty() && inputs.pitot_heat_powered[0];

    if (pitot_blocked_failure_) {
      // Failure injection: pitot fully blocked
      pitot_pressure = trapped_pitot_pressure_;
    } else {
      // Update trapped pressure continuously (captured at moment of blockage)
      if (pitot_ice_norm_ < 1.0f) {
        trapped_pitot_pressure_ = pitot_pressure;
      }

      // Accumulate ice: visible moisture + below 5°C + no pitot heat
      const bool icing_conditions =
        inputs.visible_moisture &&
        inputs.temperature_k < 278.15 &&
        !heat_on;

      if (icing_conditions) {
        pitot_ice_norm_ += static_cast<float>(dt_sec / ice_delay_s_);
        pitot_ice_norm_ = std::min(pitot_ice_norm_, 1.0f);
      }

      // Clear ice: heat on
      if (heat_on && pitot_ice_norm_ > 0.0f) {
        pitot_ice_norm_ -= static_cast<float>(dt_sec * ice_clear_rate_ / ice_delay_s_);
        pitot_ice_norm_ = std::max(pitot_ice_norm_, 0.0f);
      }

      // Partial blockage: blend between live and trapped pressures
      if (pitot_ice_norm_ > 0.0f) {
        pitot_pressure =
          (1.0 - pitot_ice_norm_) * pitot_pressure +
          pitot_ice_norm_ * trapped_pitot_pressure_;
      }
    }

    // ── Static pressure ──────────────────────────────────────────────────────
    const bool alt_static_on =
      !inputs.alternate_static_selected.empty() &&
      inputs.alternate_static_selected[0];

    double effective_static;
    if (static_blocked_failure_ && !alt_static_on) {
      // Blocked port with normal static selected: pressure frozen
      effective_static = trapped_static_pressure_;
    } else if (alt_static_on) {
      // Alternate static port (cabin) — slightly lower pressure than ambient
      effective_static = inputs.static_pressure_pa +
        static_cast<double>(alternate_offset_pa_);
      // Also bypasses a blocked normal port
      if (!static_blocked_failure_) {
        trapped_static_pressure_ = inputs.static_pressure_pa;
      }
    } else {
      effective_static = inputs.static_pressure_pa;
      trapped_static_pressure_ = inputs.static_pressure_pa;
    }

    if (effective_static <= 0.0) effective_static = 1.0;

    // ── Instrument computations ──────────────────────────────────────────────
    double diff_pressure = pitot_pressure - effective_static;
    if (diff_pressure < 0.0) diff_pressure = 0.0;

    // IAS: incompressible Bernoulli at sea-level density
    double ias = std::sqrt(2.0 * diff_pressure / rho_sl);

    // CAS = IAS for now (position error correction deferred)
    double cas = ias;

    // Mach from subsonic pitot-static ratio
    // Subsonic: M = sqrt(5 * ((1 + qc/P)^(2/7) - 1))  where qc = diff_pressure
    double mach = 0.0;
    {
      double pressure_ratio = diff_pressure / effective_static;
      if (pressure_ratio > 0.0) {
        double term = std::pow(1.0 + pressure_ratio, 2.0 / 7.0) - 1.0;
        if (term > 0.0) {
          mach = std::sqrt(5.0 * term);
        }
      }
    }

    // Pressure altitude from static pressure (ISA barometric formula)
    double pressure_alt = (T_sl / L) *
      (1.0 - std::pow(effective_static / P_sl, R * L / g));

    // Indicated altitude: correct pressure altitude for QNH deviation
    // Using hydrostatic approximation: dh ≈ -(dP) / (rho * g)
    double qnh_correction = (inputs.qnh_pa - P_sl) / (rho_sl * g);
    double indicated_alt = pressure_alt + qnh_correction;

    // VSI: first-order lag on rate of change of indicated altitude
    double raw_vsi = (indicated_alt - prev_indicated_alt_) / dt_sec;
    double lag = static_cast<double>(vsi_lag_s_);
    vsi_filtered_ += (raw_vsi - vsi_filtered_) * dt_sec / lag;
    prev_indicated_alt_ = indicated_alt;

    // With blocked static and normal static selected: VSI reads zero (no pressure change)
    if (static_blocked_failure_ && !alt_static_on) {
      vsi_filtered_ = 0.0;
    }

    // SAT and TAT
    double sat = inputs.temperature_k > 0.0 ? inputs.temperature_k : T_sl;
    double tat = sat * (1.0 + 0.2 * mach * mach);  // TAT = SAT * (1 + (γ-1)/2 * M²)

    // ── Store in system state ────────────────────────────────────────────────
    system_.indicated_airspeed_ms   = static_cast<float>(ias);
    system_.calibrated_airspeed_ms  = static_cast<float>(cas);
    system_.mach                    = static_cast<float>(mach);
    system_.altitude_indicated_m    = static_cast<float>(indicated_alt);
    system_.altitude_pressure_m     = static_cast<float>(pressure_alt);
    system_.vertical_speed_ms       = static_cast<float>(vsi_filtered_);
    system_.sat_k                   = static_cast<float>(sat);
    system_.tat_k                   = static_cast<float>(tat);
    system_.pitot_healthy           = !pitot_blocked_failure_ && pitot_ice_norm_ < 1.0f;
    system_.static_healthy          = !static_blocked_failure_;
    system_.pitot_heat_on           = heat_on;
    system_.alternate_static_active = alt_static_on;
    system_.pitot_ice_norm           = pitot_ice_norm_;
  }

  sim_interfaces::AirDataSnapshot get_snapshot() const override
  {
    sim_interfaces::AirDataSnapshot snap;
    snap.systems.push_back(system_);
    return snap;
  }

  void reset() override
  {
    pitot_blocked_failure_   = false;
    drain_blocked_           = false;
    static_blocked_failure_  = false;
    pitot_ice_norm_           = 0.0f;
    trapped_pitot_pressure_  = 101325.0;
    trapped_static_pressure_ = 101325.0;
    prev_indicated_alt_      = 0.0;
    vsi_filtered_            = 0.0;
    noise_state_             = 0.0;

    // Reset system state to defaults
    system_ = sim_interfaces::AirDataSystemState();
    system_.name = system_name_;
  }

  void apply_failure(const std::string & failure_id, bool active) override
  {
    if (failure_id == "pitot_blocked" ||
        failure_id == "pitot_blocked_drain_clear")
    {
      pitot_blocked_failure_ = active;
      drain_blocked_ = false;
      if (!active) {
        // On clearing: restore to current ambient (drain clears the pressure)
        trapped_pitot_pressure_ = 101325.0;
      }
    } else if (failure_id == "pitot_blocked_drain_blocked") {
      pitot_blocked_failure_ = active;
      drain_blocked_ = true;
      // Drain blocked: trapped pressure stays exactly at freeze-point
      // (do not touch trapped_pitot_pressure_ — it was captured at blockage)
    } else if (failure_id == "static_port_blocked") {
      static_blocked_failure_ = active;
      if (!active) {
        trapped_static_pressure_ = 101325.0;
      }
    }
    // Unknown failure IDs are silently ignored — another node may handle them
  }

  std::vector<std::string> get_heat_load_names() const override
  {
    return {heat_load_name_};
  }

  std::vector<std::string> get_alternate_static_switch_ids() const override
  {
    return {alternate_switch_id_};
  }

private:
  // XorShift64 PRNG — simple, fast, good statistical properties
  double generate_noise_sample()
  {
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 7;
    rng_state_ ^= rng_state_ << 17;
    // Map to [-1.0, 1.0]
    return (static_cast<double>(rng_state_) /
            static_cast<double>(std::numeric_limits<uint64_t>::max())) * 2.0 - 1.0;
  }

  // Config (from YAML)
  std::string system_name_      = "primary";
  std::string heat_load_name_   = "pitot_heat";
  std::string alternate_switch_id_ = "sw_alt_static";
  float ice_delay_s_            = 45.0f;
  float ice_clear_rate_         = 2.0f;
  float alternate_offset_pa_    = -30.0f;
  float pitot_noise_gain_       = 0.15f;
  float vsi_lag_s_              = 1.5f;

  // Runtime state
  sim_interfaces::AirDataSystemState system_;
  float pitot_ice_norm_          = 0.0f;
  double trapped_pitot_pressure_ = 101325.0;
  double trapped_static_pressure_ = 101325.0;
  bool pitot_blocked_failure_   = false;
  bool drain_blocked_           = false;
  bool static_blocked_failure_  = false;
  double prev_indicated_alt_    = 0.0;
  double vsi_filtered_          = 0.0;
  double noise_state_           = 0.0;
  uint64_t rng_state_           = 0x123456789abcdefULL;
};

}  // namespace aircraft_c172

PLUGINLIB_EXPORT_CLASS(aircraft_c172::AirDataModel, sim_interfaces::IAirDataModel)
