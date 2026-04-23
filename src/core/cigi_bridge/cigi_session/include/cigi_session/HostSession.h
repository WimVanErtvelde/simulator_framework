// Host-side CIGI 3.3 session. Owns a CigiHostSession internally and produces
// wire-format (big-endian) datagrams for transmission to the IG. Callers
// build a frame as BeginFrame → Append… → FinishFrame; IG Control is emitted
// as the first packet automatically.
//
// Design note: per CIGI 3.3 every Host→IG datagram must begin with IG
// Control. Individual CCL packet Pack() methods produce host byte order, not
// wire format — only CigiOutgoingMsg performs the host↔wire swap via
// CIGI_SCOPY macros. So emitters must go through a session; they cannot
// stand alone.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace cigi_session {

class ISofProcessor;
class IHatHotRespProcessor;

class HostSession {
public:
    HostSession();
    ~HostSession();
    HostSession(const HostSession &) = delete;
    HostSession & operator=(const HostSession &) = delete;

    // ── Outbound (Host → IG) ─────────────────────────────────────────────
    // Start a new message by appending an IG Control packet. Must be called
    // before any Append… call.
    //
    //   frame_cntr   — host's monotonically increasing frame counter
    //   ig_mode      — 0=Standby/Reset, 1=Operate, 2=Debug (wire value)
    //   timestamp_s  — host IG-control timestamp (seconds)
    void BeginFrame(std::uint32_t frame_cntr, std::uint8_t ig_mode,
                    double timestamp_s);

    // Append an Entity Control (state=Active, attach=Detach, alpha=255).
    // Must follow BeginFrame.
    void AppendEntityCtrl(std::uint16_t entity_id,
                           float roll_deg, float pitch_deg, float yaw_deg,
                           double lat_deg, double lon_deg, double alt_m);

    // Append a HAT/HOT Request (Extended, Geodetic). Must follow BeginFrame.
    void AppendHatHotRequest(std::uint16_t request_id,
                              double lat_deg, double lon_deg);

    // Append an Atmosphere Control (§4.1.10). AtmosphericModelEnable is
    // always false — the host supplies explicit values.
    struct AtmosphereFields {
        std::uint8_t humidity_pct;
        float        temperature_c;
        float        visibility_m;
        float        horiz_wind_ms;
        float        vert_wind_ms;
        float        wind_direction_deg;
        float        barometric_pressure_hpa;
    };
    void AppendAtmosphereControl(const AtmosphereFields & f);

    // Append a Weather Control (§4.1.8). Scope controls whether the entry
    // is global (region_id ignored), regional (region_id indexes an
    // Environmental Region), or entity-attached.
    enum class WeatherScope : std::uint8_t {
        Global   = 0,
        Regional = 1,
        Entity   = 2,
    };
    struct WeatherCtrlFields {
        std::uint16_t region_id;
        std::uint8_t  layer_id;
        std::uint8_t  humidity_pct;
        bool          weather_enable;
        bool          scud_enable;
        std::uint8_t  cloud_type;
        WeatherScope  scope;
        std::uint8_t  severity;
        float         air_temp_c;
        float         visibility_m;
        float         scud_frequency_pct;
        float         coverage_pct;
        float         base_elevation_m;
        float         thickness_m;
        float         transition_band_m;
        float         horiz_wind_ms;
        float         vert_wind_ms;
        float         wind_direction_deg;
        float         barometric_pressure_hpa;
        float         aerosol_concentration_gm3;
    };
    void AppendWeatherControl(const WeatherCtrlFields & f);

    // Append an Environmental Region Control (§4.1.9, 48 bytes). Defines a
    // rectangular (with corner rounding) region for regional weather.
    enum class RegionState : std::uint8_t {
        Inactive = 0, Active = 1, Destroyed = 2,
    };
    void AppendEnvRegionControl(std::uint16_t region_id, RegionState state,
                                 bool merge_weather,
                                 double lat_deg, double lon_deg,
                                 float size_x_m, float size_y_m,
                                 float corner_radius_m, float rotation_deg,
                                 float transition_perimeter_m);

    // Finalise the current frame and return a pointer to the wire-format
    // datagram and its length. The buffer is owned by this session and
    // remains valid until the next BeginFrame call.
    std::pair<const std::uint8_t *, std::size_t> FinishFrame();

    // ── Inbound (IG → Host) ──────────────────────────────────────────────
    void SetSofProcessor(ISofProcessor * proc);
    void SetHatHotRespProcessor(IHatHotRespProcessor * proc);

    // Parse a received datagram, dispatching each packet to its processor.
    // Returns the number of packets successfully parsed.
    std::size_t HandleDatagram(const std::uint8_t * data, std::size_t len);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cigi_session
