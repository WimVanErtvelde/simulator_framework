#include "cigi_bridge/weather_sync.hpp"
#include "cigi_bridge/weather_encoder.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace cigi_bridge
{

static constexpr uint8_t REGION_STATE_DESTROYED = 2;
static constexpr uint8_t REGION_STATE_ACTIVE    = 1;

static constexpr uint8_t CLOUD_LAYER_ID_BASE      = 1;
static constexpr uint8_t WIND_LAYER_ID_BASE       = 10;
static constexpr uint8_t SCALAR_LAYER_ID_VIS_TEMP = 20;
static constexpr uint8_t SCALAR_LAYER_ID_PRECIP   = 21;

// ─────────────────────────────────────────────────────────────────────────────
// Emit Region(Active) + all layers of a single patch into buffer.
// All-or-nothing: returns 0 if full patch doesn't fit in capacity.
// ─────────────────────────────────────────────────────────────────────────────
size_t WeatherSync::emit_patch_active(
    const sim_msgs::msg::WeatherPatch & patch,
    uint8_t * buffer, size_t capacity) const
{
    size_t required =
        48 +
        56 * std::min<size_t>(patch.cloud_layers.size(), MAX_CLOUD_LAYERS) +
        56 * std::min<size_t>(patch.wind_layers.size(),  MAX_WIND_LAYERS) +
        (patch.override_visibility  ? 56 : 0) +
        (patch.override_temperature && !patch.override_visibility ? 56 : 0) +
        (patch.override_precipitation ? 56 : 0);
    // Note: vis+temp share one packet (layer 20). If only temperature is set,
    // it still takes one packet. The logic above over-counts by 56 if both
    // vis and temp are set separately — correcting here:
    if (patch.override_visibility && patch.override_temperature) {
        required -= 56;  // single combined packet, not two
    }

    if (required > capacity) return 0;

    size_t offset = 0;
    size_t n;

    n = encode_region_control(
        buffer + offset, capacity - offset, patch, REGION_STATE_ACTIVE);
    if (n == 0) return 0;
    offset += n;

    size_t n_cloud = std::min<size_t>(patch.cloud_layers.size(), MAX_CLOUD_LAYERS);
    for (size_t i = 0; i < n_cloud; ++i) {
        n = encode_regional_cloud_layer(
            buffer + offset, capacity - offset,
            patch.patch_id, static_cast<uint8_t>(CLOUD_LAYER_ID_BASE + i),
            patch.cloud_layers[i], /*enable=*/true);
        if (n == 0) return 0;
        offset += n;
    }

    size_t n_wind = std::min<size_t>(patch.wind_layers.size(), MAX_WIND_LAYERS);
    for (size_t i = 0; i < n_wind; ++i) {
        n = encode_regional_wind_layer(
            buffer + offset, capacity - offset,
            patch.patch_id, static_cast<uint8_t>(WIND_LAYER_ID_BASE + i),
            patch.wind_layers[i], /*enable=*/true);
        if (n == 0) return 0;
        offset += n;
    }

    if (patch.override_visibility || patch.override_temperature) {
        float vis  = patch.override_visibility  ? patch.visibility_m  : std::nanf("");
        float temp = patch.override_temperature ? patch.temperature_k : std::nanf("");
        n = encode_regional_scalar_override(
            buffer + offset, capacity - offset,
            patch.patch_id, SCALAR_LAYER_ID_VIS_TEMP,
            vis, temp, std::nanf(""), 0, /*enable=*/true);
        if (n == 0) return 0;
        offset += n;
    }

    if (patch.override_precipitation) {
        n = encode_regional_scalar_override(
            buffer + offset, capacity - offset,
            patch.patch_id, SCALAR_LAYER_ID_PRECIP,
            std::nanf(""), std::nanf(""),
            patch.precipitation_rate, patch.precipitation_type,
            /*enable=*/true);
        if (n == 0) return 0;
        offset += n;
    }

    return offset;
}

// ─────────────────────────────────────────────────────────────────────────────
size_t WeatherSync::emit_patch_destroyed(
    uint16_t patch_id,
    uint8_t * buffer, size_t capacity) const
{
    sim_msgs::msg::WeatherPatch stub;
    stub.patch_id = patch_id;
    stub.radius_m = 0.0f;
    return encode_region_control(buffer, capacity, stub, REGION_STATE_DESTROYED);
}

// ─────────────────────────────────────────────────────────────────────────────
// Drain destroy_retries_: emit one Region(Destroyed) per entry with remaining>0,
// decrement, prune zeroed entries. Returns bytes written.
// ─────────────────────────────────────────────────────────────────────────────
size_t WeatherSync::emit_pending_destroys(uint8_t * buffer, size_t capacity)
{
    size_t offset = 0;
    auto it = destroy_retries_.begin();
    while (it != destroy_retries_.end()) {
        if (it->remaining == 0) {
            it = destroy_retries_.erase(it);
            continue;
        }
        size_t n = emit_patch_destroyed(it->patch_id, buffer + offset, capacity - offset);
        if (n == 0) {
            // Buffer full — leave remainder for next call
            break;
        }
        offset += n;
        it->remaining -= 1;
        if (it->remaining == 0) {
            it = destroy_retries_.erase(it);
        } else {
            ++it;
        }
    }
    return offset;
}

// ─────────────────────────────────────────────────────────────────────────────
size_t WeatherSync::process_update(
    const sim_msgs::msg::WeatherState & weather,
    uint8_t * buffer,
    size_t capacity)
{
    if (buffer == nullptr || capacity == 0) return 0;

    size_t offset = 0;

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

    // 1. Drain pending destroys from PREVIOUS calls (runs before new removals
    //    so we don't consume retries we're about to queue).
    offset += emit_pending_destroys(buffer + offset, capacity - offset);

    // 2. Find removals — sent patches not in current set → emit initial
    //    destroy now, queue remaining retries for subsequent calls.
    for (auto it = sent_patches_.begin(); it != sent_patches_.end(); ) {
        if (current_by_id.find(it->first) == current_by_id.end()) {
            size_t n = emit_patch_destroyed(it->first, buffer + offset, capacity - offset);
            if (n > 0) {
                offset += n;
                if (DESTROY_RETRY_COUNT > 1) {
                    destroy_retries_.push_back({it->first, DESTROY_RETRY_COUNT - 1});
                }
            } else {
                destroy_retries_.push_back({it->first, DESTROY_RETRY_COUNT});
                log("WeatherSync: buffer full during initial destroy; queued full retry");
            }
            it = sent_patches_.erase(it);
        } else {
            ++it;
        }
    }

    // 3. Emit Active + layers for every current patch; update sent_patches_
    for (size_t i = 0; i < n_current; ++i) {
        const auto & p = weather.patches[i];
        size_t n = emit_patch_active(p, buffer + offset, capacity - offset);
        if (n == 0) {
            std::ostringstream oss;
            oss << "WeatherSync: buffer too small for patch_id="
                << p.patch_id << " — skipping (will retry on next update)";
            log(oss.str());
            continue;
        }
        offset += n;
        sent_patches_[p.patch_id] = p;
    }

    return offset;
}

// ─────────────────────────────────────────────────────────────────────────────
size_t WeatherSync::emit_reassertion(
    uint8_t * buffer,
    size_t capacity)
{
    if (buffer == nullptr || capacity == 0) return 0;

    size_t offset = 0;

    // 1. Drain pending destroys (self-heal dropped Destroy packets)
    offset += emit_pending_destroys(buffer + offset, capacity - offset);

    // 2. Re-emit Active + layers for every tracked patch
    for (const auto & kv : sent_patches_) {
        size_t n = emit_patch_active(kv.second, buffer + offset, capacity - offset);
        if (n == 0) {
            std::ostringstream oss;
            oss << "WeatherSync: buffer too small during re-assertion for patch_id="
                << kv.first;
            log(oss.str());
            break;
        }
        offset += n;
    }

    return offset;
}

// ─────────────────────────────────────────────────────────────────────────────
void WeatherSync::flush_on_reposition()
{
    sent_patches_.clear();
    destroy_retries_.clear();
    log("WeatherSync: sent_patches_ and destroy_retries_ cleared on reposition");
}

}  // namespace cigi_bridge
