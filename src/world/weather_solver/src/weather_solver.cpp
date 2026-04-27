#include "weather_solver/weather_solver.hpp"

#include "sim_interfaces/weather_convention.hpp"

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

const sim_msgs::msg::WeatherPatch * WeatherSolver::find_dominant_patch_and_weight(
    double aircraft_lat_deg, double aircraft_lon_deg,
    double & out_weight) const
{
    out_weight = 0.0;
    const sim_msgs::msg::WeatherPatch * best = nullptr;
    double best_radius_m = std::numeric_limits<double>::max();

    for (const auto & p : weather_.patches) {
        const double radius = static_cast<double>(p.radius_m);
        if (radius <= 0.0) continue;
        const double transition =
            radius * sim_interfaces::PATCH_TRANSITION_PERIMETER_FRACTION;
        const double inner = radius - transition;
        const double d = haversine_distance_m(
            aircraft_lat_deg, aircraft_lon_deg, p.lat_deg, p.lon_deg);
        double w;
        if (d >= radius) {
            // Authored radius is the hard outer boundary — no leak past it.
            continue;
        } else if (inner <= 0.0 || transition <= 1e-3) {
            // Degenerate patch (transition would consume the whole disk):
            // hard switch at radius, no ramp.
            w = 1.0;
        } else if (d <= inner) {
            w = 1.0;
        } else {
            w = (radius - d) / transition;  // 1 → 0 across [inner, radius)
        }
        // Highest weight wins; on tie, smallest radius (more specific).
        const bool better = (w > out_weight)
                         || (w == out_weight && radius < best_radius_m);
        if (better) {
            out_weight    = w;
            best          = &p;
            best_radius_m = radius;
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
    // Highest-influence patch wins (smallest radius on weight tie). Weight
    // ramps inward from the authored radius — see
    // sim_interfaces/weather_convention.hpp for the geometric rule. The
    // authored radius is the hard outer boundary of all patch effect.
    // Pointer is only valid for this call.
    double patch_weight = 0.0;
    const auto * active_patch =
        find_dominant_patch_and_weight(lat_deg, lon_deg, patch_weight);

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
    // Temperature and pressure SL values blend smoothly between global and
    // patch by patch_weight when the corresponding override flag is set,
    // eliminating the kinematic step (altimeter jump, OAT jump) at the patch
    // radius boundary. Override flag false → patch contributes 0 (global wins
    // unblended). Pressure override is altimeter-only (qnh_pa) — does NOT
    // shift P_isa or density (field QNH exercise, not density physics).
    double oat_deviation_k = 0.0;
    double qnh_pa = ISA_P0;

    if (weather_received_) {
        double temperature_sl_k = weather_.temperature_sl_k;
        if (active_patch && active_patch->override_temperature && patch_weight > 0.0) {
            temperature_sl_k = (1.0 - patch_weight) * weather_.temperature_sl_k
                             + patch_weight * active_patch->temperature_k;
        }
        oat_deviation_k = temperature_sl_k - ISA_T0;

        double pressure_sl_pa = weather_.pressure_sl_pa;
        if (active_patch && active_patch->override_pressure && patch_weight > 0.0) {
            pressure_sl_pa = (1.0 - patch_weight) * weather_.pressure_sl_pa
                           + patch_weight * active_patch->pressure_sl_pa;
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

    // ── Wind interpolation with NED blend ───────────────────────────────────
    // Patch wind override is gated by non-empty wind_layers (the "empty array
    // = inherit" convention in WeatherPatch.msg). When a patch contributes,
    // global and patch winds are interpolated independently from their own
    // layer stacks, converted to NED ambient, and blended component-wise by
    // patch_weight. Blending in NED avoids polar-direction wraparound when
    // opposing winds cancel through a transition zone.
    //
    // Gust and Dryden run a single pass on the blended ambient: the gust
    // delta is computed from blended_gust_speed - blended_sustained_speed,
    // and Dryden uses the blended turbulence intensity. This preserves
    // determinism (the modulator + Dryden are stateful) while keeping the
    // modelled phenomenon spatially consistent.
    const bool patch_wind_active = active_patch && patch_weight > 0.0
                                && !active_patch->wind_layers.empty();
    const double w_wind = patch_wind_active ? patch_weight : 0.0;

    InterpolatedWind iw_global = interpolate_wind_from(altitude_msl_m, weather_.wind_layers);
    InterpolatedWind iw_patch{};
    if (patch_wind_active) {
        iw_patch = interpolate_wind_from(altitude_msl_m, active_patch->wind_layers);
    }

    auto polar_to_ned_ambient = [](const InterpolatedWind & iw,
                                   double & N, double & E, double & D) {
        const double dr = iw.direction_deg * M_PI / 180.0;
        N = -iw.speed_ms * std::cos(dr);
        E = -iw.speed_ms * std::sin(dr);
        D = -iw.vertical_ms;  // positive vertical_ms = updraft
    };
    double gN, gE, gD; polar_to_ned_ambient(iw_global, gN, gE, gD);
    double pN = 0.0, pE = 0.0, pD = 0.0;
    if (patch_wind_active) polar_to_ned_ambient(iw_patch, pN, pE, pD);

    double ambient_north = (1.0 - w_wind) * gN + w_wind * pN;
    double ambient_east  = (1.0 - w_wind) * gE + w_wind * pE;
    double ambient_down  = (1.0 - w_wind) * gD + w_wind * pD;

    const double blended_gust_speed = (1.0 - w_wind) * iw_global.gust_speed_ms
                                    + w_wind * iw_patch.gust_speed_ms;
    const double blended_turbulence = (1.0 - w_wind) * iw_global.turbulence
                                    + w_wind * iw_patch.turbulence;
    const double blended_sustained = std::sqrt(ambient_north * ambient_north
                                             + ambient_east  * ambient_east);

    // Direction for gust + Dryden body→NED rotation: derived from blended
    // ambient NED. When opposing winds cancel through the transition zone the
    // blended magnitude can fall below the noise floor — fall back to the
    // global layer direction so gust/turbulence rotation stays defined.
    double dir_rad;
    if (blended_sustained > 1e-3) {
        dir_rad = std::atan2(-ambient_east, -ambient_north);  // FROM convention
    } else {
        dir_rad = iw_global.direction_deg * M_PI / 180.0;
    }

    // ── Gust modulation ─────────────────────────────────────────────────────
    // Modulator steps every tick to keep its state machine temporally
    // coherent; delta is zero unless blended_gust_speed > blended_sustained.
    double gust_factor = gust_.step(dt_sec);   // 0-1
    double gust_delta = 0.0;
    if (blended_gust_speed > blended_sustained) {
        gust_delta = (blended_gust_speed - blended_sustained) * gust_factor;
    }
    const double gust_north = -gust_delta * std::cos(dir_rad);
    const double gust_east  = -gust_delta * std::sin(dir_rad);

    // ── Dryden turbulence ───────────────────────────────────────────────────
    // Dryden uses tas_ms (aircraft airspeed, not the gust-modified ambient)
    // — gusts are a separate phenomenon, not a shift in the statistical
    // model's anchor. See Slice 5a-v design note.
    auto turb = dryden_.update(dt_sec, blended_turbulence, altitude_agl_m, tas_ms);

    // Rotate body-axis perturbation (u along wind, v lateral) to NED
    double dN = -turb.u_ms * std::cos(dir_rad) + turb.v_ms * std::sin(dir_rad);
    double dE = -turb.u_ms * std::sin(dir_rad) - turb.v_ms * std::cos(dir_rad);
    double dD = -turb.w_ms;

    r.wind_north_ms = ambient_north + gust_north + dN;
    r.wind_east_ms  = ambient_east  + gust_east  + dE;
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
    r.turbulence_intensity = static_cast<float>(std::clamp(blended_turbulence, 0.0, 1.0));

    // ── Runway condition index ──────────────────────────────────────────────
    // Hard switch (no blend): runway contamination is a discrete category and
    // only meaningful while on ground. Patch override applies when ownship is
    // strictly inside the patch radius (weight == 1.0); the transition zone
    // does NOT partially apply a runway contamination index. "Apply only on
    // ground" is enforced downstream in the writeback.
    uint8_t condition_idx = 0;
    if (weather_received_) {
        condition_idx = weather_.runway_condition_idx;
        if (active_patch && active_patch->override_runway && patch_weight >= 1.0) {
            condition_idx = active_patch->runway_condition_idx;
        }
    }
    r.runway_condition_idx = condition_idx;

    return r;
}

}  // namespace weather_solver
