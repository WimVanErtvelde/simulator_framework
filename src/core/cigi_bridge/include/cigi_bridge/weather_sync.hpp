#ifndef CIGI_BRIDGE__WEATHER_SYNC_HPP_
#define CIGI_BRIDGE__WEATHER_SYNC_HPP_

// WeatherSync — tracks WeatherPatches sent to the IG, diffs incoming
// WeatherState against sent state, and emits Environmental Region Control
// (0x0B) + Weather Control (0x0C) Scope=Regional packets for additions and
// removals.
//
// Event-driven only: no periodic re-assertion, no destroy retry. On a
// dedicated Host↔IG LAN, UDP packet loss is negligible; adding retry
// machinery earned more bugs than reliability (sample stacking when
// publishers stop, Destroy-then-Active race conditions, test complexity).
//
// Slice 2c.1 adds content-hash short-circuit: unchanged patches don't
// re-emit, so repeated same-content WeatherState publishes are no-ops on
// the wire.
//
// Sanity limits (framework convention):
//   - MAX_PATCHES = 10 (hard ceiling; IOS enforces realistic 5)
//   - MAX_CLOUD_LAYERS = 3 per patch
//   - MAX_WIND_LAYERS = 3 per patch
//
// Regional layer ID allocation:
//   - Cloud layers: 1, 2, 3
//   - Wind layers: 10, 11, 12
//   - Scalar overrides (visibility + temperature): 20
//   - Scalar overrides (precipitation): 21

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include <sim_msgs/msg/weather_state.hpp>
#include <sim_msgs/msg/weather_patch.hpp>

namespace cigi_bridge
{

class WeatherSync
{
public:
    using Logger = std::function<void(const std::string &)>;

    static constexpr size_t MAX_PATCHES      = 10;
    static constexpr size_t MAX_CLOUD_LAYERS = 3;
    static constexpr size_t MAX_WIND_LAYERS  = 3;

    WeatherSync() = default;
    explicit WeatherSync(Logger logger) : logger_(std::move(logger)) {}

    // Diff incoming WeatherState against sent_patches_. Emit:
    //   - Region(Destroyed) once for each sent patch not in weather.patches
    //   - Region(Active) + layers for each patch in weather.patches whose
    //     content differs from what was last sent (short-circuit on match)
    //
    // Returns bytes written.
    size_t process_update(
        const sim_msgs::msg::WeatherState & weather,
        uint8_t * buffer,
        size_t capacity);

    // Clear sent-state. Call on reposition rising edge — IG is being reset.
    void flush_on_reposition();

    // Test / inspection
    size_t sent_count() const { return sent_patches_.size(); }

private:
    // Internal helpers — emit into buffer, return bytes or 0 on insufficient capacity
    size_t emit_patch_active(
        const sim_msgs::msg::WeatherPatch & patch,
        uint8_t * buffer, size_t capacity) const;
    size_t emit_patch_destroyed(
        uint16_t patch_id,
        uint8_t * buffer, size_t capacity) const;

    std::map<uint16_t, sim_msgs::msg::WeatherPatch> sent_patches_;
    Logger logger_;

    void log(const std::string & msg) const
    {
        if (logger_) logger_(msg);
    }
};

}  // namespace cigi_bridge

#endif  // CIGI_BRIDGE__WEATHER_SYNC_HPP_
