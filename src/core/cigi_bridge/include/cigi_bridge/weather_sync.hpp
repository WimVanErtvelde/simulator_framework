#ifndef CIGI_BRIDGE__WEATHER_SYNC_HPP_
#define CIGI_BRIDGE__WEATHER_SYNC_HPP_

// WeatherSync — tracks which WeatherPatch IDs have been sent to the IG and
// emits Environmental Region Control (0x0B) + Weather Control (0x0C) Scope=
// Regional packets for additions and removals relative to the previous state.
//
// Slice 2b scope: event-driven emit on each process_update() call, reposition
// flush, sanity limits. No content hashing, no periodic re-assertion, no
// destroy retry — those are Slice 2c.
//
// Sanity limits (framework convention):
//   - Max 5 local patches per WeatherState (framework-enforced at IOS; this
//     class caps at 10 as a hard sanity ceiling — anything above is dropped
//     with a warning logged via the supplied logger callback).
//   - Max 3 cloud layers per patch (X-Plane / CIGI 0x0C convention).
//   - Max 3 wind layers per patch (framework convention: surface / mid / high).
//
// Regional layer ID allocation within a patch:
//   - Cloud layers: CIGI 1, 2, 3 (standard cloud layer IDs)
//   - Wind layers:  10, 11, 12 (IG-defined range per CIGI 3.3)
//   - Scalar overrides (visibility + temperature): layer ID 20
//   - Scalar overrides (precipitation):            layer ID 21

#include <cstdint>
#include <functional>
#include <set>
#include <string>

#include <sim_msgs/msg/weather_state.hpp>

namespace cigi_bridge
{

class WeatherSync
{
public:
    // Logger callback signature: receives a message string. Caller wires this
    // to RCLCPP_WARN / RCLCPP_INFO as desired. Nullable (no logging if null).
    using Logger = std::function<void(const std::string &)>;

    // Framework limits (exposed as public constants for test reference).
    static constexpr size_t MAX_PATCHES          = 10;  // hard sanity ceiling
    static constexpr size_t MAX_CLOUD_LAYERS     = 3;
    static constexpr size_t MAX_WIND_LAYERS      = 3;

    WeatherSync() = default;
    explicit WeatherSync(Logger logger) : logger_(std::move(logger)) {}

    // Process a new WeatherState snapshot. Computes the diff between
    // weather.patches and the internally-tracked sent_ids_. Emits:
    //   - Region(Active) + layers for every patch in weather.patches
    //     (always — no content-hash short-circuit in Slice 2b)
    //   - Region(Destroyed) for every patch_id in sent_ids_ that is not
    //     present in weather.patches
    //
    // Returns the number of bytes written to buffer. Returns 0 if buffer
    // is too small to fit even the first packet; partial writes do not occur.
    //
    // After this call, sent_ids_ == set of patch_ids in weather.patches.
    size_t process_update(
        const sim_msgs::msg::WeatherState & weather,
        uint8_t * buffer,
        size_t capacity);

    // Clear the sent-ID set. Call on reposition rising edge — the IG will be
    // reset, so previously-sent state is no longer live. Next process_update()
    // will re-emit everything as fresh additions.
    void flush_on_reposition();

    // Test / inspection helper — number of currently-tracked patch IDs.
    size_t sent_count() const { return sent_ids_.size(); }

private:
    std::set<uint16_t> sent_ids_;
    Logger logger_;

    void log(const std::string & msg) const
    {
        if (logger_) logger_(msg);
    }
};

}  // namespace cigi_bridge

#endif  // CIGI_BRIDGE__WEATHER_SYNC_HPP_
