#include "cigi_bridge/weather_sync.hpp"
#include "cigi_bridge/weather_encoder.hpp"

#include <cmath>
#include <sstream>

namespace cigi_bridge
{

// Region State values per CIGI 3.3 ICD section 4.1.11
static constexpr uint8_t REGION_STATE_DESTROYED = 2;
static constexpr uint8_t REGION_STATE_ACTIVE    = 1;

// Framework layer-ID allocation within a regional patch
static constexpr uint8_t CLOUD_LAYER_ID_BASE      = 1;   // 1, 2, 3
static constexpr uint8_t WIND_LAYER_ID_BASE       = 10;  // 10, 11, 12
static constexpr uint8_t SCALAR_LAYER_ID_VIS_TEMP = 20;
static constexpr uint8_t SCALAR_LAYER_ID_PRECIP   = 21;

// ─────────────────────────────────────────────────────────────────────────────
// Emit Region(Active) + all layers of a single patch into buffer.
// Returns bytes written. Returns 0 if the full patch doesn't fit; partial
// emission is NOT performed (all-or-nothing to avoid orphaned Region packets).
// ─────────────────────────────────────────────────────────────────────────────
static size_t emit_patch_active(
    const sim_msgs::msg::WeatherPatch & patch,
    uint8_t * buffer,
    size_t capacity)
{
    // Pre-compute required size to fit everything before emitting anything.
    size_t required =
        48 +                                                   // Region Control
        56 * std::min<size_t>(patch.cloud_layers.size(), WeatherSync::MAX_CLOUD_LAYERS) +
        56 * std::min<size_t>(patch.wind_layers.size(),  WeatherSync::MAX_WIND_LAYERS) +
        (patch.override_visibility  ? 56 : 0) +
        (patch.override_temperature ? 56 : 0) +
        (patch.override_precipitation ? 56 : 0);

    if (required > capacity) return 0;

    size_t offset = 0;

    // 1. Region Control (State=Active)
    size_t n = encode_region_control(
        buffer + offset, capacity - offset, patch, REGION_STATE_ACTIVE);
    if (n == 0) return 0;
    offset += n;

    // 2. Cloud layers (capped at MAX_CLOUD_LAYERS)
    size_t n_cloud = std::min<size_t>(patch.cloud_layers.size(), WeatherSync::MAX_CLOUD_LAYERS);
    for (size_t i = 0; i < n_cloud; ++i) {
        uint8_t layer_id = static_cast<uint8_t>(CLOUD_LAYER_ID_BASE + i);
        n = encode_regional_cloud_layer(
            buffer + offset, capacity - offset,
            patch.patch_id, layer_id, patch.cloud_layers[i], /*enable=*/true);
        if (n == 0) return 0;
        offset += n;
    }

    // 3. Wind layers (capped at MAX_WIND_LAYERS)
    size_t n_wind = std::min<size_t>(patch.wind_layers.size(), WeatherSync::MAX_WIND_LAYERS);
    for (size_t i = 0; i < n_wind; ++i) {
        uint8_t layer_id = static_cast<uint8_t>(WIND_LAYER_ID_BASE + i);
        n = encode_regional_wind_layer(
            buffer + offset, capacity - offset,
            patch.patch_id, layer_id, patch.wind_layers[i], /*enable=*/true);
        if (n == 0) return 0;
        offset += n;
    }

    // 4. Visibility + temperature combined into one Scope=Regional Weather Control
    //    (both share the same CIGI packet slots, layer_id=20).
    if (patch.override_visibility || patch.override_temperature) {
        float vis  = patch.override_visibility  ? patch.visibility_m  : std::nanf("");
        float temp = patch.override_temperature ? patch.temperature_k : std::nanf("");
        n = encode_regional_scalar_override(
            buffer + offset, capacity - offset,
            patch.patch_id, SCALAR_LAYER_ID_VIS_TEMP,
            vis, temp,
            /*precip_rate=*/std::nanf(""), /*precip_type=*/0,
            /*enable=*/true);
        if (n == 0) return 0;
        offset += n;
    }

    // 5. Precipitation override (layer_id=21)
    if (patch.override_precipitation) {
        n = encode_regional_scalar_override(
            buffer + offset, capacity - offset,
            patch.patch_id, SCALAR_LAYER_ID_PRECIP,
            /*vis=*/std::nanf(""), /*temp=*/std::nanf(""),
            patch.precipitation_rate, patch.precipitation_type,
            /*enable=*/true);
        if (n == 0) return 0;
        offset += n;
    }

    return offset;
}

// ─────────────────────────────────────────────────────────────────────────────
// Emit Region(Destroyed) for a removed patch. Only the Region Control packet
// is emitted — destroying the region implicitly disables all its layers per
// CIGI 3.3 ICD section 4.1.11 (Region State: Destroyed).
// ─────────────────────────────────────────────────────────────────────────────
static size_t emit_patch_destroyed(
    uint16_t patch_id,
    uint8_t * buffer,
    size_t capacity)
{
    sim_msgs::msg::WeatherPatch stub;
    stub.patch_id = patch_id;
    // lat/lon/radius irrelevant for Destroyed state; zeros are fine
    stub.radius_m = 0.0f;
    return encode_region_control(buffer, capacity, stub, REGION_STATE_DESTROYED);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────
size_t WeatherSync::process_update(
    const sim_msgs::msg::WeatherState & weather,
    uint8_t * buffer,
    size_t capacity)
{
    if (buffer == nullptr || capacity == 0) return 0;

    size_t offset = 0;

    // Determine current patch set (capped at MAX_PATCHES)
    const size_t n_current = std::min<size_t>(weather.patches.size(), MAX_PATCHES);
    if (weather.patches.size() > MAX_PATCHES) {
        std::ostringstream oss;
        oss << "WeatherSync: received " << weather.patches.size()
            << " patches; truncating to MAX_PATCHES=" << MAX_PATCHES;
        log(oss.str());
    }

    std::set<uint16_t> current_ids;
    for (size_t i = 0; i < n_current; ++i) {
        current_ids.insert(weather.patches[i].patch_id);
    }

    // 1. Emit Destroyed for every sent ID not in current set
    for (uint16_t sent_id : sent_ids_) {
        if (current_ids.find(sent_id) == current_ids.end()) {
            size_t n = emit_patch_destroyed(sent_id, buffer + offset, capacity - offset);
            if (n == 0) {
                log("WeatherSync: buffer full while emitting Destroyed — state divergence possible");
                // sent_ids_ will still be updated to current_ids below; the
                // un-emitted Destroy means a zombie region on the IG until
                // Slice 2c periodic re-assertion handles it.
                break;
            }
            offset += n;
        }
    }

    // 2. Emit Active + layers for every current patch
    for (size_t i = 0; i < n_current; ++i) {
        size_t n = emit_patch_active(weather.patches[i], buffer + offset, capacity - offset);
        if (n == 0) {
            std::ostringstream oss;
            oss << "WeatherSync: buffer too small for patch_id="
                << weather.patches[i].patch_id << " — skipping";
            log(oss.str());
            continue;  // try to fit remaining patches
        }
        offset += n;
    }

    // 3. Update sent state to reflect what's now on the wire.
    //    Patches whose Active emission was skipped due to buffer exhaustion
    //    are still recorded here — they will re-emit on the next
    //    process_update() call because the IOS-driven weather_dirty_ re-fires
    //    whenever IOS republishes /world/weather.
    sent_ids_ = std::move(current_ids);

    return offset;
}

void WeatherSync::flush_on_reposition()
{
    sent_ids_.clear();
    log("WeatherSync: sent_ids_ cleared on reposition");
}

}  // namespace cigi_bridge
