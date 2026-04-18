#ifndef CIGI_BRIDGE__WEATHER_ENCODER_HPP_
#define CIGI_BRIDGE__WEATHER_ENCODER_HPP_

#include <sim_msgs/msg/weather_state.hpp>
#include <sim_msgs/msg/weather_patch.hpp>
#include <sim_msgs/msg/weather_cloud_layer.hpp>
#include <sim_msgs/msg/weather_wind_layer.hpp>
#include <vector>
#include <cstdint>

namespace cigi_bridge
{

/// Encode CIGI 3.3 weather packets from WeatherState into the provided buffer.
/// Returns number of bytes written. Buffer must have sufficient capacity.
/// Appends: one Atmosphere Control (32B) + N Weather Control packets (56B each).
size_t encode_weather_packets(
    uint8_t * buffer,
    size_t buffer_capacity,
    const sim_msgs::msg::WeatherState & weather);

// ─────────────────────────────────────────────────────────────────────────────
// Regional weather (CIGI 3.3 Environmental Region Control + Scope=Regional
// Weather Control). Used for WeatherPatch — per-patch localized weather.
// ─────────────────────────────────────────────────────────────────────────────

/// Encode an Environmental Region Control packet (0x0B, 48 bytes) for a patch.
/// Region is circular: SizeX=SizeY=0, CornerRadius=radius_m. Region State is
/// set from the region_state argument (0=Inactive, 1=Active, 2=Destroyed).
/// Returns bytes written (48) on success, 0 on insufficient capacity.
size_t encode_region_control(
    uint8_t * buffer,
    size_t buffer_capacity,
    const sim_msgs::msg::WeatherPatch & patch,
    uint8_t region_state);

/// Encode a single Weather Control packet (0x0C, 56 bytes) with Scope=Regional,
/// referencing the patch's patch_id as the CIGI Region ID. The layer_id and
/// per-layer fields are supplied by the caller — this function does not iterate
/// over the patch's layer arrays. Caller is responsible for invoking once per
/// cloud layer / wind layer / scalar override group they want to emit.
/// Returns bytes written (56) on success, 0 on insufficient capacity.
size_t encode_regional_cloud_layer(
    uint8_t * buffer,
    size_t buffer_capacity,
    uint16_t region_id,
    uint8_t layer_id,                               // CIGI 1-3 for cloud layers
    const sim_msgs::msg::WeatherCloudLayer & layer,
    bool weather_enable);

/// Encode Weather Control (0x0C) Scope=Regional for a wind-only layer. The
/// cloud_type field is set to None(0); wind fields are populated from layer.
size_t encode_regional_wind_layer(
    uint8_t * buffer,
    size_t buffer_capacity,
    uint16_t region_id,
    uint8_t layer_id,                               // CIGI 10+ for IG-defined
    const sim_msgs::msg::WeatherWindLayer & layer,
    bool weather_enable);

/// Encode Weather Control (0x0C) Scope=Regional for a scalar override (visibility,
/// temperature, or precipitation). Uses a dedicated layer_id per override type.
/// The exact layer_ids are a framework convention — Slice 2b will wire callers
/// to use consistent IDs across add/remove cycles.
size_t encode_regional_scalar_override(
    uint8_t * buffer,
    size_t buffer_capacity,
    uint16_t region_id,
    uint8_t layer_id,
    float visibility_m,                             // NaN = ignore
    float temperature_k,                            // NaN = ignore
    float precipitation_rate,                       // NaN = ignore
    uint8_t precipitation_type,                     // 0 = ignore
    bool weather_enable);

}  // namespace cigi_bridge

#endif  // CIGI_BRIDGE__WEATHER_ENCODER_HPP_
