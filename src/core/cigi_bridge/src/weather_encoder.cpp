#include "cigi_bridge/weather_encoder.hpp"

#include <cstring>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Big-endian write helpers (same pattern as cigi_host_node.cpp)
// ─────────────────────────────────────────────────────────────────────────────
static void write_be16(uint8_t * p, uint16_t v)
{
    p[0] = (v >> 8) & 0xFF;
    p[1] =  v       & 0xFF;
}

static void write_be_float(uint8_t * p, float v)
{
    uint32_t u;
    memcpy(&u, &v, sizeof u);
    p[0] = (u >> 24) & 0xFF;
    p[1] = (u >> 16) & 0xFF;
    p[2] = (u >>  8) & 0xFF;
    p[3] =  u        & 0xFF;
}

static void write_be_float64(uint8_t * p, double v)
{
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    for (int i = 0; i < 8; ++i) {
        p[i] = (u >> (56 - 8 * i)) & 0xFF;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CIGI 3.3 packet constants
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint8_t CIGI_PKT_ATMO_CTRL    = 10;   // 0x0A
static constexpr uint8_t CIGI_ATMO_CTRL_SIZE   = 32;

static constexpr uint8_t CIGI_PKT_WEATHER_CTRL = 12;   // 0x0C
static constexpr uint8_t CIGI_WEATHER_CTRL_SIZE = 56;

// User-defined — runway surface friction (visual wetness / ice / snow)
static constexpr uint8_t CIGI_PKT_RUNWAY_FRICTION  = 0xCB;
static constexpr uint8_t CIGI_RUNWAY_FRICTION_SIZE = 8;

static constexpr uint8_t CIGI_PKT_ENV_REGION_CTRL  = 11;  // 0x0B
static constexpr uint8_t CIGI_ENV_REGION_CTRL_SIZE = 48;

// Scope values for Weather Control packet
static constexpr uint8_t CIGI_SCOPE_GLOBAL   = 0;
static constexpr uint8_t CIGI_SCOPE_REGIONAL = 1;
static constexpr uint8_t CIGI_SCOPE_ENTITY   = 2;

// Region State values for Environmental Region Control
static constexpr uint8_t CIGI_REGION_STATE_INACTIVE  = 0;
static constexpr uint8_t CIGI_REGION_STATE_ACTIVE    = 1;
static constexpr uint8_t CIGI_REGION_STATE_DESTROYED = 2;

// Framework convention: layer IDs for scalar overrides in regional Weather Control.
// Cloud layers use CIGI standard 1-3 (mapped directly from WeatherCloudLayer index).
// Wind layers use 10+ (IG-defined range per CIGI 3.3).
// Scalar overrides use these reserved IDs:
static constexpr uint8_t CIGI_LAYER_ID_SCALAR_VIS_TEMP = 20;  // visibility + temperature
static constexpr uint8_t CIGI_LAYER_ID_SCALAR_PRECIP   = 21;  // precipitation

// ─────────────────────────────────────────────────────────────────────────────
// Atmosphere Control (Packet ID = 10, 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────
static size_t encode_atmosphere_control(
    uint8_t * buf, size_t capacity,
    const sim_msgs::msg::WeatherState & w)
{
    if (capacity < CIGI_ATMO_CTRL_SIZE) return 0;
    memset(buf, 0, CIGI_ATMO_CTRL_SIZE);

    buf[0] = CIGI_PKT_ATMO_CTRL;
    buf[1] = CIGI_ATMO_CTRL_SIZE;
    buf[2] = 0;  // Atmospheric Model Enable = 0 (no FASCODE/MODTRAN)
    buf[3] = static_cast<uint8_t>(std::clamp(static_cast<int>(w.humidity_pct), 0, 100));

    // Global air temperature (°C) — convert from K
    write_be_float(&buf[4],  static_cast<float>(w.temperature_sl_k - 273.15));
    // Global visibility range (m)
    write_be_float(&buf[8],  static_cast<float>(w.visibility_m));

    // Surface wind from first wind layer
    float h_wind = 0.0f, v_wind = 0.0f, wind_dir = 0.0f;
    if (!w.wind_layers.empty()) {
        h_wind  = w.wind_layers[0].wind_speed_ms;
        v_wind  = w.wind_layers[0].vertical_wind_ms;
        wind_dir = w.wind_layers[0].wind_direction_deg;
    }
    write_be_float(&buf[12], h_wind);    // Horizontal wind speed (m/s)
    write_be_float(&buf[16], v_wind);    // Vertical wind speed (m/s)
    write_be_float(&buf[20], wind_dir);  // Wind direction (° true, FROM)

    // Barometric pressure (mbar = hPa)
    write_be_float(&buf[24], static_cast<float>(w.pressure_sl_pa / 100.0));
    // Bytes 28-31: reserved (already zeroed)

    return CIGI_ATMO_CTRL_SIZE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Weather Control (Packet ID = 12, 56 bytes)
// ─────────────────────────────────────────────────────────────────────────────
static size_t encode_weather_control(
    uint8_t * buf, size_t capacity,
    uint8_t layer_id,
    uint8_t cloud_type,
    float coverage_pct,
    float base_elev_m,
    float thickness_m,
    float transition_band_m,
    bool scud_enable,
    float scud_freq_pct,
    float h_wind_ms,
    float v_wind_ms,
    float wind_dir_deg,
    float turbulence_severity,
    float air_temp_c,
    float visibility_m,
    float baro_pressure_mbar)
{
    if (capacity < CIGI_WEATHER_CTRL_SIZE) return 0;
    memset(buf, 0, CIGI_WEATHER_CTRL_SIZE);

    buf[0] = CIGI_PKT_WEATHER_CTRL;
    buf[1] = CIGI_WEATHER_CTRL_SIZE;
    // Bytes 2-3: Entity/Region ID = 0 (global)
    write_be16(&buf[2], 0);
    buf[4] = layer_id;
    buf[5] = 0;  // Humidity (per-layer, 0 = use global)

    // Flags byte 6: Weather Enable (bit0), Scud Enable (bit1),
    //   Random Winds (bit2)=0, Random Lightning (bit3)=0,
    //   Cloud Type (bits 4-7)
    uint8_t flags = 0x01;  // Weather Enable = 1
    if (scud_enable) flags |= 0x02;
    flags |= static_cast<uint8_t>((cloud_type & 0x0F) << 4);
    buf[6] = flags;

    // Flags byte 7: Scope (bits 0-1) = 0 (Global),
    //   Severity (bits 2-4) from turbulence_severity 0-1 → 0-5
    uint8_t severity = static_cast<uint8_t>(std::clamp(turbulence_severity * 5.0f, 0.0f, 5.0f));
    buf[7] = static_cast<uint8_t>((severity & 0x07) << 2);

    write_be_float(&buf[8],  air_temp_c);
    write_be_float(&buf[12], visibility_m);
    write_be_float(&buf[16], scud_freq_pct);
    write_be_float(&buf[20], coverage_pct);
    write_be_float(&buf[24], base_elev_m);
    write_be_float(&buf[28], thickness_m);
    write_be_float(&buf[32], transition_band_m);
    write_be_float(&buf[36], h_wind_ms);
    write_be_float(&buf[40], v_wind_ms);
    write_be_float(&buf[44], wind_dir_deg);
    write_be_float(&buf[48], baro_pressure_mbar);
    write_be_float(&buf[52], 0.0f);  // Aerosol Concentration (g/m³)

    return CIGI_WEATHER_CTRL_SIZE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Find the nearest wind layer to a given altitude
// ─────────────────────────────────────────────────────────────────────────────
static const sim_msgs::msg::WeatherWindLayer * find_nearest_wind(
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

// ─────────────────────────────────────────────────────────────────────────────
namespace cigi_bridge
{

size_t encode_weather_packets(
    uint8_t * buffer,
    size_t buffer_capacity,
    const sim_msgs::msg::WeatherState & weather)
{
    size_t offset = 0;

    // Global atmosphere scalars
    float global_temp_c = static_cast<float>(weather.temperature_sl_k - 273.15);
    float global_vis_m  = static_cast<float>(weather.visibility_m);
    float global_baro   = static_cast<float>(weather.pressure_sl_pa / 100.0);

    // 1. Atmosphere Control (always, one per frame)
    offset += encode_atmosphere_control(buffer + offset, buffer_capacity - offset, weather);

    // 2. Cloud layers → Weather Control packets (layer_id 1-3).
    // Always emit exactly 3 packets so removed layers are explicitly disabled on the IG.
    size_t num_clouds = std::min(weather.cloud_layers.size(), static_cast<size_t>(3));
    for (size_t i = 0; i < 3; ++i) {
        if (i < num_clouds) {
            const auto & cl = weather.cloud_layers[i];
            float center_alt = cl.base_elevation_m + cl.thickness_m * 0.5f;

            // Wind at cloud layer altitude
            float h_wind = 0.0f, v_wind = 0.0f, wind_dir = 0.0f, turb = 0.0f;
            const auto * wl = find_nearest_wind(weather.wind_layers, center_alt);
            if (wl) {
                h_wind  = wl->wind_speed_ms;
                v_wind  = wl->vertical_wind_ms;
                wind_dir = wl->wind_direction_deg;
                turb    = wl->turbulence_severity;
            }

            offset += encode_weather_control(
                buffer + offset, buffer_capacity - offset,
                static_cast<uint8_t>(i + 1),  // layer_id 1-3
                cl.cloud_type,
                cl.coverage_pct,
                cl.base_elevation_m,
                cl.thickness_m,
                cl.transition_band_m,
                cl.scud_enable,
                cl.scud_frequency_pct,
                h_wind, v_wind, wind_dir, turb,
                global_temp_c,
                global_vis_m,
                global_baro);
        } else {
            // Layer not present — emit explicit disable packet (weather_enable=0).
            if (buffer_capacity - offset < CIGI_WEATHER_CTRL_SIZE) continue;
            uint8_t * p = buffer + offset;
            memset(p, 0, CIGI_WEATHER_CTRL_SIZE);
            p[0] = CIGI_PKT_WEATHER_CTRL;
            p[1] = CIGI_WEATHER_CTRL_SIZE;
            p[4] = static_cast<uint8_t>(i + 1);  // layer_id
            // p[6] bit 0 (Weather Enable) = 0, all other fields zero
            offset += CIGI_WEATHER_CTRL_SIZE;
        }
    }

    // 3. Precipitation layer (layer_id 4=Rain, 5=Snow)
    if (weather.precipitation_rate > 0.0f && weather.precipitation_type > 0) {
        uint8_t precip_layer_id = (weather.precipitation_type == 2) ? 5 : 4;
        float precip_base = 0.0f;
        float precip_thickness = 3000.0f;  // default span
        if (!weather.cloud_layers.empty()) {
            precip_thickness = weather.cloud_layers[0].base_elevation_m;
            if (precip_thickness < 100.0f) precip_thickness = 3000.0f;
        }

        offset += encode_weather_control(
            buffer + offset, buffer_capacity - offset,
            precip_layer_id,
            0,  // cloud_type = None (precipitation, not cloud)
            weather.precipitation_rate * 100.0f,  // coverage from rate
            precip_base,
            precip_thickness,
            0.0f, false, 0.0f,  // no transition/scud
            0.0f, 0.0f, 0.0f, 0.0f,  // no per-layer wind
            global_temp_c,
            global_vis_m,
            global_baro);
    }

    // 3b. Runway friction (user-defined 0xCB — 8 bytes)
    if (buffer_capacity - offset >= CIGI_RUNWAY_FRICTION_SIZE) {
        uint8_t * p = buffer + offset;
        memset(p, 0, CIGI_RUNWAY_FRICTION_SIZE);
        p[0] = CIGI_PKT_RUNWAY_FRICTION;
        p[1] = CIGI_RUNWAY_FRICTION_SIZE;
        p[2] = weather.runway_friction;
        // Bytes 3-7: reserved
        offset += CIGI_RUNWAY_FRICTION_SIZE;
    }

    // 4. Wind-only layers (layer_id 10+)
    size_t max_winds = std::min(weather.wind_layers.size(), static_cast<size_t>(13));
    for (size_t j = 0; j < max_winds; ++j) {
        const auto & wl = weather.wind_layers[j];
        offset += encode_weather_control(
            buffer + offset, buffer_capacity - offset,
            static_cast<uint8_t>(10 + j),  // layer_id 10+
            0,    // cloud_type = None
            0.0f, // coverage = 0 (no cloud, wind only)
            wl.altitude_msl_m,
            0.0f, // thickness = 0
            0.0f, false, 0.0f,  // no transition/scud
            wl.wind_speed_ms,
            wl.vertical_wind_ms,
            wl.wind_direction_deg,
            wl.turbulence_severity,
            global_temp_c,
            global_vis_m,
            global_baro);
    }

    return offset;
}

}  // namespace cigi_bridge

// ─────────────────────────────────────────────────────────────────────────────
// Environmental Region Control (Packet ID = 11, 48 bytes)
//
// Byte layout per CIGI 3.3 ICD section 4.1.11:
//   0      Packet ID (11)
//   1      Packet Size (48)
//   2-3    Region ID (uint16 BE)
//   4      Flags byte:
//            bits 0-1 Region State (2=Destroyed, 1=Active, 0=Inactive)
//            bit  2   Merge Weather Properties
//            bit  3   Merge Aerosol Concentrations
//            bit  4   Merge Maritime Surface Conditions
//            bit  5   Merge Terrestrial Surface Conditions
//            bits 6-7 Reserved
//   5-7    Reserved
//   8-15   Latitude (float64 BE, degrees)
//   16-23  Longitude (float64 BE, degrees)
//   24-27  Size X (float32 BE, meters; 0 for circle)
//   28-31  Size Y (float32 BE, meters; 0 for circle)
//   32-35  Corner Radius (float32 BE, meters; = radius for circle)
//   36-39  Rotation (float32 BE, degrees; 0 for circle)
//   40-43  Transition Perimeter (float32 BE, meters)
//   44-47  Reserved
// ─────────────────────────────────────────────────────────────────────────────
namespace cigi_bridge
{

size_t encode_region_control(
    uint8_t * buf, size_t capacity,
    const sim_msgs::msg::WeatherPatch & patch,
    uint8_t region_state)
{
    if (capacity < CIGI_ENV_REGION_CTRL_SIZE) return 0;
    memset(buf, 0, CIGI_ENV_REGION_CTRL_SIZE);

    buf[0] = CIGI_PKT_ENV_REGION_CTRL;
    buf[1] = CIGI_ENV_REGION_CTRL_SIZE;

    write_be16(&buf[2], patch.patch_id);

    // Flags byte: region state (bits 0-1) + merge_weather (bit 2) = 1
    uint8_t flags = (region_state & 0x03) | (1u << 2);
    buf[4] = flags;
    // Bytes 5-7 reserved (already zeroed)

    write_be_float64(&buf[8],  patch.lat_deg);
    write_be_float64(&buf[16], patch.lon_deg);

    // Circular region: SizeX = SizeY = 0, CornerRadius = radius_m
    write_be_float(&buf[24], 0.0f);
    write_be_float(&buf[28], 0.0f);
    write_be_float(&buf[32], patch.radius_m);
    write_be_float(&buf[36], 0.0f);                      // Rotation (irrelevant for circle)
    write_be_float(&buf[40], patch.radius_m * 0.10f);    // Transition Perimeter = 10% of radius
    // Bytes 44-47 reserved (already zeroed)

    return CIGI_ENV_REGION_CTRL_SIZE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Weather Control (Packet ID = 12, 56 bytes) — Scope=Regional variants
//
// Byte layout per CIGI 3.3 ICD section 4.1.12:
//   0      Packet ID (12)
//   1      Packet Size (56)
//   2-3    Entity ID / Region ID (uint16 BE)
//   4      Layer ID (uint8)
//   5      Humidity (uint8, 0-100 %)
//   6      Flags byte:
//            bit  0   Weather Enable
//            bit  1   Scud Enable
//            bit  2   Random Winds Enable
//            bit  3   Random Lightning Enable
//            bits 4-7 Cloud Type (enum 0-15)
//   7      Scope + Severity + Reserved:
//            bits 0-1 Scope (0=Global, 1=Regional, 2=Entity)
//            bits 2-4 Severity (0-5)
//            bits 5-7 Reserved
//   8-11   Air Temperature (float BE, °C)
//   12-15  Visibility Range (float BE, meters)
//   16-19  Scud Frequency (float BE, %)
//   20-23  Coverage (float BE, %)
//   24-27  Base Elevation (float BE, meters MSL)
//   28-31  Thickness (float BE, meters)
//   32-35  Transition Band (float BE, meters)
//   36-39  Horizontal Wind Speed (float BE, m/s)
//   40-43  Vertical Wind Speed (float BE, m/s)
//   44-47  Wind Direction (float BE, degrees true, FROM)
//   48-51  Barometric Pressure (float BE, mbar)
//   52-55  Aerosol Concentration (float BE, reserved)
// ─────────────────────────────────────────────────────────────────────────────

static size_t encode_weather_control_regional_base(
    uint8_t * buf, size_t capacity,
    uint16_t region_id,
    uint8_t layer_id,
    bool weather_enable,
    uint8_t cloud_type)
{
    if (capacity < CIGI_WEATHER_CTRL_SIZE) return 0;
    memset(buf, 0, CIGI_WEATHER_CTRL_SIZE);

    buf[0] = CIGI_PKT_WEATHER_CTRL;
    buf[1] = CIGI_WEATHER_CTRL_SIZE;
    write_be16(&buf[2], region_id);
    buf[4] = layer_id;
    buf[5] = 0;  // Humidity — default, overridden below if caller populates
    uint8_t flags = (weather_enable ? 0x01 : 0x00) | ((cloud_type & 0x0F) << 4);
    buf[6] = flags;
    buf[7] = CIGI_SCOPE_REGIONAL & 0x03;  // Scope=Regional, Severity=0

    return CIGI_WEATHER_CTRL_SIZE;
}

size_t encode_regional_cloud_layer(
    uint8_t * buf, size_t capacity,
    uint16_t region_id,
    uint8_t layer_id,
    const sim_msgs::msg::WeatherCloudLayer & layer,
    bool weather_enable)
{
    size_t n = encode_weather_control_regional_base(
        buf, capacity, region_id, layer_id, weather_enable, layer.cloud_type);
    if (n == 0) return 0;

    // Scud enable (bit 1 of flags at buf[6])
    if (layer.scud_enable) buf[6] |= 0x02;

    write_be_float(&buf[12], 0.0f);                                   // vis (not overridden here)
    write_be_float(&buf[16], layer.scud_frequency_pct);
    write_be_float(&buf[20], layer.coverage_pct);
    write_be_float(&buf[24], layer.base_elevation_m);
    write_be_float(&buf[28], layer.thickness_m);
    write_be_float(&buf[32], layer.transition_band_m);
    // Wind fields zeroed (bytes 36-47) — this is a cloud layer
    return n;
}

size_t encode_regional_wind_layer(
    uint8_t * buf, size_t capacity,
    uint16_t region_id,
    uint8_t layer_id,
    const sim_msgs::msg::WeatherWindLayer & layer,
    bool weather_enable)
{
    size_t n = encode_weather_control_regional_base(
        buf, capacity, region_id, layer_id, weather_enable, 0 /*cloud_type=None*/);
    if (n == 0) return 0;

    // Cloud/coverage fields zeroed (this is a wind layer)
    // Base elevation = altitude of wind layer
    write_be_float(&buf[24], layer.altitude_msl_m);
    write_be_float(&buf[36], layer.wind_speed_ms);
    write_be_float(&buf[40], layer.vertical_wind_ms);
    write_be_float(&buf[44], layer.wind_direction_deg);
    return n;
}

size_t encode_regional_scalar_override(
    uint8_t * buf, size_t capacity,
    uint16_t region_id,
    uint8_t layer_id,
    float visibility_m,
    float temperature_k,
    float precipitation_rate,
    uint8_t precipitation_type,
    bool weather_enable)
{
    (void)precipitation_rate;
    (void)precipitation_type;

    size_t n = encode_weather_control_regional_base(
        buf, capacity, region_id, layer_id, weather_enable, 0 /*cloud_type=None*/);
    if (n == 0) return 0;

    if (!std::isnan(temperature_k)) {
        write_be_float(&buf[8], static_cast<float>(temperature_k - 273.15));
    }
    if (!std::isnan(visibility_m)) {
        write_be_float(&buf[12], visibility_m);
    }
    // Precipitation rate/type are stored in this packet via layer_id=4(Rain)/5(Snow)
    // per the CIGI convention already used in the global weather encoder; Slice 2b
    // will emit a separate Weather Control with the precip layer_id when the
    // override_precipitation flag is set. This function is for vis+temp overrides;
    // precipitation overrides use this same function with layer_id=4 or 5 but are
    // dispatched by Slice 2b. The precipitation_rate and precipitation_type
    // parameters are retained in the signature for symmetry and future use.
    return n;
}

}  // namespace cigi_bridge
