#ifndef CIGI_BRIDGE__WEATHER_SYNC_HPP_
#define CIGI_BRIDGE__WEATHER_SYNC_HPP_

// WeatherSync — tracks WeatherPatches sent to the IG, diffs incoming
// WeatherState against sent state, and appends Environmental Region Control
// + Weather Control (Scope=Regional) packets to a HostSession for additions
// and removals.
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

#include "cigi_session/HostSession.h"

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

    // Diff incoming WeatherState against sent_patches_ and append packets to
    // the provided HostSession (must have an active BeginFrame context):
    //   - Env Region (Destroyed) once for each sent patch not in weather.patches
    //   - Env Region (Active) + Weather Control (Scope=Regional) layers for
    //     each patch in weather.patches whose content differs from what was
    //     last sent (short-circuit on match)
    void process_update(
        const sim_msgs::msg::WeatherState & weather,
        cigi_session::HostSession & session);

    // Clear sent-state. Call on reposition rising edge — IG is being reset.
    void flush_on_reposition();

    // Test / inspection
    size_t sent_count() const { return sent_patches_.size(); }

private:
    // Internal helpers — append packets for one patch to the session.
    void emit_patch_active(
        const sim_msgs::msg::WeatherPatch & patch,
        cigi_session::HostSession & session) const;
    void emit_patch_destroyed(
        uint16_t patch_id,
        cigi_session::HostSession & session) const;

    std::map<uint16_t, sim_msgs::msg::WeatherPatch> sent_patches_;
    Logger logger_;

    void log(const std::string & msg) const
    {
        if (logger_) logger_(msg);
    }
};

}  // namespace cigi_bridge

#endif  // CIGI_BRIDGE__WEATHER_SYNC_HPP_
