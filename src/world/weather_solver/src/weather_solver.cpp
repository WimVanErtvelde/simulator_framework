#include "weather_solver/weather_solver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace weather_solver
{

// ─── Helpers ────────────────────────────────────────────────────────────────

/// Angular interpolation using shortest arc (handles 350° ↔ 010° wrap).
static double interp_angle(double a_deg, double b_deg, double frac)
{
    double diff = b_deg - a_deg;
    // Wrap to [-180, 180]
    while (diff >  180.0) diff -= 360.0;
    while (diff < -180.0) diff += 360.0;
    double result = a_deg + frac * diff;
    while (result < 0.0)   result += 360.0;
    while (result >= 360.0) result -= 360.0;
    return result;
}

double WeatherSolver::haversine_distance_m(
    double lat1_deg, double lon1_deg,
    double lat2_deg, double lon2_deg)
{
    constexpr double EARTH_RADIUS_M = 6371000.0;
    constexpr double DEG_TO_RAD = M_PI / 180.0;

    double lat1 = lat1_deg * DEG_TO_RAD;
    double lat2 = lat2_deg * DEG_TO_RAD;
    double dlat = (lat2_deg - lat1_deg) * DEG_TO_RAD;
    double dlon = (lon2_deg - lon1_deg) * DEG_TO_RAD;

    double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0)
             + std::cos(lat1) * std::cos(lat2)
               * std::sin(dlon / 2.0) * std::sin(dlon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return EARTH_RADIUS_M * c;
}

const sim_msgs::msg::WeatherPatch * WeatherSolver::find_active_patch(
    double aircraft_lat_deg, double aircraft_lon_deg) const
{
    const sim_msgs::msg::WeatherPatch * best = nullptr;
    float best_radius_m = std::numeric_limits<float>::max();

    for (const auto & p : weather_.patches) {
        double dist = haversine_distance_m(
            aircraft_lat_deg, aircraft_lon_deg,
            p.lat_deg, p.lon_deg);
        if (dist <= static_cast<double>(p.radius_m) && p.radius_m < best_radius_m) {
            best = &p;
            best_radius_m = p.radius_m;
        }
    }
    return best;
}

// ─── Public ─────────────────────────────────────────────────────────────────

void WeatherSolver::configure(const Config & /*cfg*/)
{
    reset();
}

void WeatherSolver::set_weather(const sim_msgs::msg::WeatherState & weather)
{
    weather_ = weather;
    weather_received_ = true;

    // Configure Dryden from weather turbulence model + seed
    DrydenTurbulence::Config dc;
    dc.model = static_cast<DrydenTurbulence::Model>(weather.turbulence_model);
    dc.seed  = weather.deterministic_seed;
    dryden_.configure(dc);

    // Gust modulator — same seed as Dryden for full determinism, but an
    // independent mt19937 stream so gust timing doesn't couple to turbulence.
    GustModulator::Config gc;
    gc.seed = weather.deterministic_seed;
    gust_.configure(gc);

    // Cache microburst parameters (lat/lon → NE conversion deferred to compute())
    mb_params_.clear();
    mb_params_.reserve(weather.microbursts.size());
    for (const auto & mb : weather.microbursts) {
        MicroburstModel::HazardParams hp;
        hp.core_radius_m     = mb.core_radius_m;
        hp.shaft_altitude_m  = mb.shaft_altitude_m;
        hp.intensity         = mb.intensity;
        hp.lifecycle_phase   = mb.lifecycle_phase;
        hp.activation_time_sec = mb.activation_time_sec;
        // Store raw lat/lon in north/east temporarily — converted in compute()
        hp.north_m = mb.latitude_deg;
        hp.east_m  = mb.longitude_deg;
        mb_params_.push_back(hp);
    }
}

void WeatherSolver::reset()
{
    weather_ = sim_msgs::msg::WeatherState();
    weather_received_ = false;
    dryden_.reset();
    gust_.reset();
    mb_params_.clear();
}

WeatherSolver::InterpolatedWind WeatherSolver::interpolate_wind_from(
    double altitude_m,
    const std::vector<sim_msgs::msg::WeatherWindLayer> & layers) const
{
    InterpolatedWind result{};
    if (layers.empty()) return result;

    // Build sorted index by altitude
    struct Entry {
        double alt;
        size_t idx;
    };
    std::vector<Entry> sorted;
    sorted.reserve(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        sorted.push_back({static_cast<double>(layers[i].altitude_msl_m), i});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const Entry & a, const Entry & b) { return a.alt < b.alt; });

    // Below lowest layer
    if (altitude_m <= sorted.front().alt) {
        const auto & wl = layers[sorted.front().idx];
        result.speed_ms      = wl.wind_speed_ms;
        result.direction_deg = wl.wind_direction_deg;
        result.vertical_ms   = wl.vertical_wind_ms;
        result.gust_speed_ms = wl.gust_speed_ms;
        result.turbulence    = wl.turbulence_severity;
        return result;
    }

    // Above highest layer
    if (altitude_m >= sorted.back().alt) {
        const auto & wl = layers[sorted.back().idx];
        result.speed_ms      = wl.wind_speed_ms;
        result.direction_deg = wl.wind_direction_deg;
        result.vertical_ms   = wl.vertical_wind_ms;
        result.gust_speed_ms = wl.gust_speed_ms;
        result.turbulence    = wl.turbulence_severity;
        return result;
    }

    // Find bracketing layers
    for (size_t i = 0; i + 1 < sorted.size(); ++i) {
        if (altitude_m >= sorted[i].alt && altitude_m <= sorted[i + 1].alt) {
            const auto & lo = layers[sorted[i].idx];
            const auto & hi = layers[sorted[i + 1].idx];
            double span = sorted[i + 1].alt - sorted[i].alt;
            double frac = (span > 0.0) ? (altitude_m - sorted[i].alt) / span : 0.0;

            result.speed_ms      = lo.wind_speed_ms + frac * (hi.wind_speed_ms - lo.wind_speed_ms);
            result.direction_deg = interp_angle(lo.wind_direction_deg, hi.wind_direction_deg, frac);
            result.vertical_ms   = lo.vertical_wind_ms + frac * (hi.vertical_wind_ms - lo.vertical_wind_ms);
            result.gust_speed_ms = lo.gust_speed_ms + frac * (hi.gust_speed_ms - lo.gust_speed_ms);
            result.turbulence    = lo.turbulence_severity + frac * (hi.turbulence_severity - lo.turbulence_severity);
            return result;
        }
    }

    return result;
}

WeatherSolver::AtmoResult WeatherSolver::compute(
    double dt_sec, double altitude_msl_m, double altitude_agl_m, double tas_ms,
    double lat_deg, double lon_deg, double sim_time_sec)
{
    AtmoResult r{};

    double alt = std::clamp(altitude_msl_m, -610.0, 20000.0);

    // ── Patch lookup ────────────────────────────────────────────────────────
    // Smallest-radius containing patch wins when the aircraft is inside
    // multiple overlapping patches. Pointer is only valid for this call.
    const auto * active_patch = find_active_patch(lat_deg, lon_deg);

    // ── ISA computation ─────────────────────────────────────────────────────
    double T_isa, P_isa;
    if (alt <= ISA_TROPO_ALT) {
        T_isa = ISA_T0 - ISA_LAPSE * alt;
        P_isa = ISA_P0 * std::pow(T_isa / ISA_T0, ISA_G / (ISA_LAPSE * ISA_R));
    } else {
        T_isa = ISA_T_TROPO;
        P_isa = ISA_P_TROPO * std::exp(-ISA_G / (ISA_R * ISA_T_TROPO) * (alt - ISA_TROPO_ALT));
    }

    // ── Apply weather deviations ────────────────────────────────────────────
    // Temperature and pressure overrides come from the active patch when the
    // corresponding override flag is set; otherwise global SL values apply.
    // Pressure override is altimeter-only (qnh_pa) — it does NOT shift P_isa
    // or density (field QNH exercise, not density physics). Slice 5b-iv.
    double oat_deviation_k = 0.0;
    double qnh_pa = ISA_P0;

    if (weather_received_) {
        double temperature_sl_k = weather_.temperature_sl_k;
        if (active_patch && active_patch->override_temperature) {
            temperature_sl_k = active_patch->temperature_k;
        }
        oat_deviation_k = temperature_sl_k - ISA_T0;

        double pressure_sl_pa = weather_.pressure_sl_pa;
        if (active_patch && active_patch->override_pressure) {
            pressure_sl_pa = active_patch->pressure_sl_pa;
        }
        qnh_pa = pressure_sl_pa;
    }

    double oat_k = T_isa + oat_deviation_k;
    double density = P_isa / (ISA_R * oat_k);
    double speed_of_sound = std::sqrt(ISA_GAMMA * ISA_R * oat_k);

    double pressure_altitude = HYPSO_SCALE * (1.0 - std::pow(P_isa / ISA_P0, HYPSO_P_EXP));
    double density_altitude  = HYPSO_SCALE * (1.0 - std::pow(density / ISA_RHO0, HYPSO_D_EXP));

    r.temperature_k       = T_isa;
    r.pressure_pa         = P_isa;
    r.density_kgm3        = density;
    r.speed_of_sound_ms   = speed_of_sound;
    r.oat_k               = oat_k;
    r.qnh_pa              = qnh_pa;
    r.density_altitude_m  = density_altitude;
    r.pressure_altitude_m = pressure_altitude;

    // ── Wind interpolation ──────────────────────────────────────────────────
    // Patch wind override: non-empty patch.wind_layers REPLACES global
    // (not merge) — matches "empty array = inherit" convention in
    // WeatherPatch.msg documentation.
    const auto & wind_source =
        (active_patch && !active_patch->wind_layers.empty())
            ? active_patch->wind_layers
            : weather_.wind_layers;
    InterpolatedWind iw = interpolate_wind_from(altitude_msl_m, wind_source);

    // ── Gust modulation ─────────────────────────────────────────────────────
    // Gusts only apply when authored peak > sustained. The modulator always
    // steps to keep its state machine temporally coherent; the applied delta
    // is zero when gust_speed_ms <= speed_ms.
    double gust_factor = gust_.step(dt_sec);   // 0-1
    double gust_delta = 0.0;
    if (iw.gust_speed_ms > iw.speed_ms) {
        gust_delta = (iw.gust_speed_ms - iw.speed_ms) * gust_factor;
    }
    double effective_speed_ms = iw.speed_ms + gust_delta;

    // Convert polar → NED (FROM convention)
    double dir_rad = iw.direction_deg * M_PI / 180.0;
    double ambient_north = -effective_speed_ms * std::cos(dir_rad);
    double ambient_east  = -effective_speed_ms * std::sin(dir_rad);
    double ambient_down  = -iw.vertical_ms;  // positive vertical_ms = updraft

    // ── Dryden turbulence ───────────────────────────────────────────────────
    // Dryden uses tas_ms (aircraft airspeed, not the gust-modified ambient)
    // — gusts are a separate phenomenon, not a shift in the statistical
    // model's anchor. See Slice 5a-v design note.
    auto turb = dryden_.update(dt_sec, iw.turbulence, altitude_agl_m, tas_ms);

    // Rotate body-axis perturbation (u along wind, v lateral) to NED
    double dN = -turb.u_ms * std::cos(dir_rad) + turb.v_ms * std::sin(dir_rad);
    double dE = -turb.u_ms * std::sin(dir_rad) - turb.v_ms * std::cos(dir_rad);
    double dD = -turb.w_ms;

    r.wind_north_ms = ambient_north + dN;
    r.wind_east_ms  = ambient_east  + dE;
    r.wind_down_ms  = ambient_down  + dD;

    // ── Microburst contributions ────────────────────────────────────────────
    if (!mb_params_.empty()) {
        // Convert microburst lat/lon to NE offsets relative to aircraft position
        // (aircraft as reference — works because we only need relative distances)
        std::vector<MicroburstModel::HazardParams> resolved = mb_params_;
        for (auto & hp : resolved) {
            double mb_lat = hp.north_m;  // stored raw lat in set_weather()
            double mb_lon = hp.east_m;   // stored raw lon in set_weather()
            MicroburstModel::latlon_to_ne(lat_deg, lon_deg, mb_lat, mb_lon,
                                          hp.north_m, hp.east_m);
        }
        // Aircraft at origin (0,0) since we used it as reference
        auto mb_wind = MicroburstModel::sample(resolved, 0.0, 0.0,
                                               altitude_agl_m, sim_time_sec);
        r.wind_north_ms += mb_wind.north_ms;
        r.wind_east_ms  += mb_wind.east_ms;
        r.wind_down_ms  += mb_wind.down_ms;
    }

    r.visible_moisture     = false;  // proper computation deferred
    r.turbulence_intensity = static_cast<float>(std::clamp(iw.turbulence, 0.0, 1.0));

    // ── Runway condition index ──────────────────────────────────────────────
    // Patch override replaces global when inside a patch with override_runway.
    // "Apply only on ground" is enforced downstream in the writeback (uses
    // aircraft on_ground state); here we just publish the regional value.
    uint8_t condition_idx = 0;
    if (weather_received_) {
        condition_idx = weather_.runway_condition_idx;
        if (active_patch && active_patch->override_runway) {
            condition_idx = active_patch->runway_condition_idx;
        }
    }
    r.runway_condition_idx = condition_idx;

    return r;
}

}  // namespace weather_solver
