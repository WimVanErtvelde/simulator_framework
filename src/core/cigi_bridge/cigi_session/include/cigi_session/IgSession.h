// IG-side CIGI 3.3 session. Mirrors HostSession but from the Image
// Generator's perspective — emits Start of Frame (SOF) + HAT/HOT Extended
// Response, dispatches incoming IG Control / Entity Control / HAT/HOT
// Request / Atmosphere / EnvRegion / Weather Control / Component Control to
// registered processors.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace cigi_session {

class IIgCtrlProcessor;
class IEntityCtrlProcessor;
class IHatHotReqProcessor;
class IAtmosphereProcessor;
class IEnvRegionProcessor;
class IWeatherCtrlProcessor;
class ICompCtrlProcessor;

class IgSession {
public:
    IgSession();
    ~IgSession();
    IgSession(const IgSession &) = delete;
    IgSession & operator=(const IgSession &) = delete;

    // ── Outbound (IG → Host) ─────────────────────────────────────────────
    // Start a new IG→Host datagram by appending an SOF packet.
    //   ig_mode          — 0=Standby/Reset, 1=Operate, 2=Debug, 3=Offline
    //   database_id      — loaded database identifier
    //   ig_frame_number  — IG's monotonically increasing frame counter
    //   last_host_frame  — echoed host frame counter for latency tracking
    void BeginFrame(std::uint8_t  ig_mode,
                    std::int8_t   database_id,
                    std::uint32_t ig_frame_number,
                    std::uint32_t last_host_frame);

    // Append a HAT/HOT Extended Response (§4.2.3, 40 bytes).
    void AppendHatHotXResp(std::uint16_t request_id,
                            bool valid,
                            double hat_m, double hot_m,
                            std::uint32_t material_code,
                            float normal_azimuth_deg,
                            float normal_elevation_deg);

    // Finalise the current frame and return a pointer to the wire-format
    // datagram and its length. The buffer is owned by this session and
    // remains valid until the next BeginFrame call.
    std::pair<const std::uint8_t *, std::size_t> FinishFrame();

    // ── Inbound (Host → IG) ──────────────────────────────────────────────
    void SetIgCtrlProcessor(IIgCtrlProcessor * p);
    void SetEntityCtrlProcessor(IEntityCtrlProcessor * p);
    void SetHatHotReqProcessor(IHatHotReqProcessor * p);
    void SetAtmosphereProcessor(IAtmosphereProcessor * p);
    void SetEnvRegionProcessor(IEnvRegionProcessor * p);
    void SetWeatherCtrlProcessor(IWeatherCtrlProcessor * p);
    void SetCompCtrlProcessor(ICompCtrlProcessor * p);

    std::size_t HandleDatagram(const std::uint8_t * data, std::size_t len);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cigi_session
