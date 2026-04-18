#ifndef CIGI_BRIDGE__WEATHER_SYNC_HPP_
#define CIGI_BRIDGE__WEATHER_SYNC_HPP_

// WeatherSync — tracks WeatherPatches sent to the IG, diffs incoming
// WeatherState against sent state, and emits Environmental Region Control
// (0x0B) + Weather Control (0x0C) Scope=Regional packets for additions and
// removals.
//
// Slice 2c adds UDP-loss-compensation features:
//   - Periodic re-assertion: emit_reassertion() re-emits Active for every
//     currently-tracked patch; caller invokes this from a ~10 s timer.
//   - Destroy retry: removed patches are queued and emitted 3× across
//     subsequent process_update / emit_reassertion calls.
//   - (Startup reset is handled by cigi_host_node, not WeatherSync.)
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
#include <vector>

#include <sim_msgs/msg/weather_state.hpp>
#include <sim_msgs/msg/weather_patch.hpp>

namespace cigi_bridge
{

class WeatherSync
{
public:
    using Logger = std::function<void(const std::string &)>;

    static constexpr size_t MAX_PATCHES          = 10;
    static constexpr size_t MAX_CLOUD_LAYERS     = 3;
    static constexpr size_t MAX_WIND_LAYERS      = 3;
    static constexpr uint32_t DESTROY_RETRY_COUNT = 3;

    WeatherSync() = default;
    explicit WeatherSync(Logger logger) : logger_(std::move(logger)) {}

    // Diff incoming WeatherState against sent_patches_. Emit:
    //   - Region(Destroyed) for each sent patch not in weather.patches
    //     (adds to destroy_retries_ for additional resends on subsequent calls)
    //   - Region(Active) + layers for each patch in weather.patches
    // Also emits a pending-destroys pass for any patches still in
    // destroy_retries_ whose retry count hasn't been exhausted.
    //
    // Returns bytes written.
    size_t process_update(
        const sim_msgs::msg::WeatherState & weather,
        uint8_t * buffer,
        size_t capacity);

    // Re-emit Region(Active) + layers for every currently-tracked patch.
    // Also emits pending destroy-retries. Intended to run on a periodic
    // timer (e.g., 10 s) to self-heal UDP loss.
    //
    // Does NOT modify sent_patches_ — the IG state is presumed correct;
    // this is a "just in case" re-send. Returns bytes written.
    size_t emit_reassertion(
        uint8_t * buffer,
        size_t capacity);

    // Clear all sent-state. Call on reposition rising edge — IG is being
    // reset. Also clears destroy_retries_ (zombie regions on the old IG
    // state are moot once it's reset).
    void flush_on_reposition();

    // Test / inspection
    size_t sent_count() const { return sent_patches_.size(); }
    bool has_pending_destroys() const { return !destroy_retries_.empty(); }
    size_t pending_destroy_count() const {
        size_t total = 0;
        for (const auto & d : destroy_retries_) total += d.remaining;
        return total;
    }

private:
    struct DestroyRetry {
        uint16_t patch_id;
        uint32_t remaining;   // calls remaining to emit (starts at DESTROY_RETRY_COUNT - 1
                              // because the initial emit in process_update counts as 1)
    };

    // Internal helpers — emit into buffer, return bytes or 0 on insufficient capacity
    size_t emit_patch_active(
        const sim_msgs::msg::WeatherPatch & patch,
        uint8_t * buffer, size_t capacity) const;
    size_t emit_patch_destroyed(
        uint16_t patch_id,
        uint8_t * buffer, size_t capacity) const;
    size_t emit_pending_destroys(
        uint8_t * buffer, size_t capacity);

    std::map<uint16_t, sim_msgs::msg::WeatherPatch> sent_patches_;
    std::vector<DestroyRetry> destroy_retries_;
    Logger logger_;

    void log(const std::string & msg) const
    {
        if (logger_) logger_(msg);
    }
};

}  // namespace cigi_bridge

#endif  // CIGI_BRIDGE__WEATHER_SYNC_HPP_
