#include "cigi_bridge/weather_sync.hpp"

#include "sim_interfaces/weather_convention.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace cigi_bridge
{

static constexpr uint8_t CLOUD_LAYER_ID_BASE      = 1;
static constexpr uint8_t WIND_LAYER_ID_BASE       = 10;
static constexpr uint8_t SCALAR_LAYER_ID_VIS_TEMP = 20;
static constexpr uint8_t SCALAR_LAYER_ID_PRECIP   = 21;

// ─────────────────────────────────────────────────────────────────────────────
// Field-by-field equality for WeatherPatch content. Used to short-circuit
// re-emit on process_update when incoming patch matches sent state.
//
// Compares all fields that contribute to the on-wire CIGI packets:
//   - Geometry: lat, lon, radius
//   - Cloud layers (size + per-layer fields)
//   - Wind layers (size + per-layer fields)
//   - Scalar override flags and values
//
// Does NOT compare: patch_id (caller matches by id), patch_type, label, icao
// (IOS-display metadata, not sent to IG), ground_elevation_m (IOS-display
// only, not sent to IG).
// ─────────────────────────────────────────────────────────────────────────────
static bool patch_content_equal(
    const sim_msgs::msg::WeatherPatch & a,
    const sim_msgs::msg::WeatherPatch & b)
{
    if (a.lat_deg   != b.lat_deg)   return false;
    if (a.lon_deg   != b.lon_deg)   return false;
    if (a.radius_m  != b.radius_m)  return false;

    if (a.cloud_layers.size() != b.cloud_layers.size()) return false;
    for (size_t i = 0; i < a.cloud_layers.size(); ++i) {
        const auto & x = a.cloud_layers[i];
        const auto & y = b.cloud_layers[i];
        if (x.cloud_type         != y.cloud_type)         return false;
        if (x.coverage_pct       != y.coverage_pct)       return false;
        if (x.base_elevation_m   != y.base_elevation_m)   return false;
        if (x.thickness_m        != y.thickness_m)        return false;
        if (x.transition_band_m  != y.transition_band_m)  return false;
        if (x.scud_enable        != y.scud_enable)        return false;
        if (x.scud_frequency_pct != y.scud_frequency_pct) return false;
    }

    if (a.wind_layers.size() != b.wind_layers.size()) return false;
    for (size_t i = 0; i < a.wind_layers.size(); ++i) {
        const auto & x = a.wind_layers[i];
        const auto & y = b.wind_layers[i];
        if (x.altitude_msl_m     != y.altitude_msl_m)     return false;
        if (x.wind_speed_ms      != y.wind_speed_ms)      return false;
        if (x.vertical_wind_ms   != y.vertical_wind_ms)   return false;
        if (x.wind_direction_deg != y.wind_direction_deg) return false;
    }

    if (a.override_visibility    != b.override_visibility)    return false;
    if (a.visibility_m           != b.visibility_m)           return false;
    if (a.override_temperature   != b.override_temperature)   return false;
    if (a.temperature_k          != b.temperature_k)          return false;
    if (a.override_precipitation != b.override_precipitation) return false;
    if (a.precipitation_rate     != b.precipitation_rate)     return false;
    if (a.precipitation_type     != b.precipitation_type)     return false;

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Append Env Region (Active) + all regional Weather Control layers for one
// patch to the session.
// ─────────────────────────────────────────────────────────────────────────────
void WeatherSync::emit_patch_active(
    const sim_msgs::msg::WeatherPatch & patch,
    cigi_session::HostSession & session) const
{
    // Circular region: SizeX=SizeY=0, CornerRadius=radius_m.
    // Transition perimeter is derived inward from the authored radius —
    // see sim_interfaces/weather_convention.hpp for the geometric rule
    // shared with the FDM-side weather_solver.
    const float tp_m = static_cast<float>(
        patch.radius_m * sim_interfaces::PATCH_TRANSITION_PERIMETER_FRACTION);
    session.AppendEnvRegionControl(
        patch.patch_id,
        cigi_session::HostSession::RegionState::Active,
        /*merge_weather=*/true,
        patch.lat_deg, patch.lon_deg,
        /*size_x_m=*/0.0f, /*size_y_m=*/0.0f,
        /*corner_radius_m=*/patch.radius_m,
        /*rotation_deg=*/0.0f,
        /*transition_perimeter_m=*/tp_m);

    // ── Cloud layers (regional Scope, layer IDs 1..3) ────────────────────
    size_t n_cloud = std::min<size_t>(patch.cloud_layers.size(), MAX_CLOUD_LAYERS);
    for (size_t i = 0; i < n_cloud; ++i) {
        const auto & cl = patch.cloud_layers[i];
        cigi_session::HostSession::WeatherCtrlFields wc{};
        wc.region_id           = patch.patch_id;
        wc.layer_id            = static_cast<uint8_t>(CLOUD_LAYER_ID_BASE + i);
        wc.humidity_pct        = 0;
        wc.weather_enable      = true;
        wc.scud_enable         = cl.scud_enable;
        wc.cloud_type          = cl.cloud_type;
        wc.scope               = cigi_session::HostSession::WeatherScope::Regional;
        wc.severity            = 0;
        wc.air_temp_c          = 0.0f;
        wc.visibility_m        = 0.0f;
        wc.scud_frequency_pct  = cl.scud_frequency_pct;
        wc.coverage_pct        = cl.coverage_pct;
        wc.base_elevation_m    = cl.base_elevation_m;
        wc.thickness_m         = cl.thickness_m;
        wc.transition_band_m   = cl.transition_band_m;
        wc.horiz_wind_ms       = 0.0f;
        wc.vert_wind_ms        = 0.0f;
        wc.wind_direction_deg  = 0.0f;
        wc.barometric_pressure_hpa   = 0.0f;
        wc.aerosol_concentration_gm3 = 0.0f;
        session.AppendWeatherControl(wc);
    }

    // ── Wind layers (regional Scope, layer IDs 10..12) ───────────────────
    size_t n_wind = std::min<size_t>(patch.wind_layers.size(), MAX_WIND_LAYERS);
    for (size_t i = 0; i < n_wind; ++i) {
        const auto & wl = patch.wind_layers[i];
        cigi_session::HostSession::WeatherCtrlFields wc{};
        wc.region_id           = patch.patch_id;
        wc.layer_id            = static_cast<uint8_t>(WIND_LAYER_ID_BASE + i);
        wc.humidity_pct        = 0;
        wc.weather_enable      = true;
        wc.scud_enable         = false;
        wc.cloud_type          = 0;  // None
        wc.scope               = cigi_session::HostSession::WeatherScope::Regional;
        wc.severity            = 0;
        wc.air_temp_c          = 0.0f;
        wc.visibility_m        = 0.0f;
        wc.scud_frequency_pct  = 0.0f;
        wc.coverage_pct        = 0.0f;
        wc.base_elevation_m    = wl.altitude_msl_m;
        wc.thickness_m         = 0.0f;
        wc.transition_band_m   = 0.0f;
        wc.horiz_wind_ms       = wl.wind_speed_ms;
        wc.vert_wind_ms        = wl.vertical_wind_ms;
        wc.wind_direction_deg  = wl.wind_direction_deg;
        wc.barometric_pressure_hpa   = 0.0f;
        wc.aerosol_concentration_gm3 = 0.0f;
        session.AppendWeatherControl(wc);
    }

    // ── Scalar override: visibility + temperature (layer 20) ─────────────
    if (patch.override_visibility || patch.override_temperature) {
        cigi_session::HostSession::WeatherCtrlFields wc{};
        wc.region_id           = patch.patch_id;
        wc.layer_id            = SCALAR_LAYER_ID_VIS_TEMP;
        wc.humidity_pct        = 0;
        wc.weather_enable      = true;
        wc.scud_enable         = false;
        wc.cloud_type          = 0;
        wc.scope               = cigi_session::HostSession::WeatherScope::Regional;
        wc.severity            = 0;
        // Unused overrides are left at 0 (encoded on wire as 0); plugin
        // gates by threshold so default-zero is ignored.
        wc.air_temp_c   = patch.override_temperature
                          ? static_cast<float>(patch.temperature_k - 273.15f)
                          : 0.0f;
        wc.visibility_m = patch.override_visibility
                          ? patch.visibility_m
                          : 0.0f;
        wc.scud_frequency_pct = 0.0f;
        wc.coverage_pct       = 0.0f;
        wc.base_elevation_m   = 0.0f;
        wc.thickness_m        = 0.0f;
        wc.transition_band_m  = 0.0f;
        wc.horiz_wind_ms      = 0.0f;
        wc.vert_wind_ms       = 0.0f;
        wc.wind_direction_deg = 0.0f;
        wc.barometric_pressure_hpa   = 0.0f;
        wc.aerosol_concentration_gm3 = 0.0f;
        session.AppendWeatherControl(wc);
    }

    // ── Scalar override: precipitation (layer 21) ────────────────────────
    if (patch.override_precipitation) {
        cigi_session::HostSession::WeatherCtrlFields wc{};
        wc.region_id           = patch.patch_id;
        wc.layer_id            = SCALAR_LAYER_ID_PRECIP;
        wc.humidity_pct        = 0;
        wc.weather_enable      = true;
        wc.scud_enable         = false;
        wc.cloud_type          = 0;
        wc.scope               = cigi_session::HostSession::WeatherScope::Regional;
        wc.severity            = 0;
        wc.air_temp_c          = 0.0f;
        wc.visibility_m        = 0.0f;
        wc.scud_frequency_pct  = 0.0f;
        wc.coverage_pct        = patch.precipitation_rate * 100.0f;
        wc.base_elevation_m    = 0.0f;
        wc.thickness_m         = 0.0f;
        wc.transition_band_m   = 0.0f;
        wc.horiz_wind_ms       = 0.0f;
        wc.vert_wind_ms        = 0.0f;
        wc.wind_direction_deg  = 0.0f;
        wc.barometric_pressure_hpa   = 0.0f;
        wc.aerosol_concentration_gm3 = 0.0f;
        session.AppendWeatherControl(wc);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void WeatherSync::emit_patch_destroyed(
    uint16_t patch_id,
    cigi_session::HostSession & session) const
{
    session.AppendEnvRegionControl(
        patch_id,
        cigi_session::HostSession::RegionState::Destroyed,
        /*merge_weather=*/true,
        /*lat_deg=*/0.0, /*lon_deg=*/0.0,
        /*size_x_m=*/0.0f, /*size_y_m=*/0.0f,
        /*corner_radius_m=*/0.0f,
        /*rotation_deg=*/0.0f,
        /*transition_perimeter_m=*/0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
void WeatherSync::process_update(
    const sim_msgs::msg::WeatherState & weather,
    cigi_session::HostSession & session)
{
    const size_t n_current = std::min<size_t>(weather.patches.size(), MAX_PATCHES);
    if (weather.patches.size() > MAX_PATCHES) {
        std::ostringstream oss;
        oss << "WeatherSync: received " << weather.patches.size()
            << " patches; truncating to MAX_PATCHES=" << MAX_PATCHES;
        log(oss.str());
    }

    // Build set of current patch_ids for diff
    std::map<uint16_t, const sim_msgs::msg::WeatherPatch *> current_by_id;
    for (size_t i = 0; i < n_current; ++i) {
        current_by_id[weather.patches[i].patch_id] = &weather.patches[i];
    }

    // 1. Find removals — sent patches not in current set → emit Destroyed once
    for (auto it = sent_patches_.begin(); it != sent_patches_.end(); ) {
        if (current_by_id.find(it->first) == current_by_id.end()) {
            emit_patch_destroyed(it->first, session);
            it = sent_patches_.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Emit Active + layers for every current patch whose content changed.
    //    Short-circuit: identical-content patches don't re-emit (Slice 2c.1).
    for (size_t i = 0; i < n_current; ++i) {
        const auto & p = weather.patches[i];

        auto sent_it = sent_patches_.find(p.patch_id);
        if (sent_it != sent_patches_.end() && patch_content_equal(p, sent_it->second)) {
            continue;
        }

        emit_patch_active(p, session);
        sent_patches_[p.patch_id] = p;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void WeatherSync::flush_on_reposition()
{
    sent_patches_.clear();
    log("WeatherSync: sent_patches_ cleared on reposition");
}

}  // namespace cigi_bridge
