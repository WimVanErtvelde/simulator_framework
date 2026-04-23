#pragma once
#include <cstdint>

namespace cigi_session {

enum class IgModeRx : std::uint8_t {
    StandbyOrReset = 0,
    Operate        = 1,
    Debug          = 2,
    Offline        = 3,
};

struct SofFields {
    IgModeRx      ig_mode;
    std::uint8_t  ig_status_code;
    std::int8_t   database_id;
    std::uint32_t ig_frame_number;
    std::uint32_t last_host_frame_number;
};

// Inbound SOF dispatch — HostSession calls OnSof once per SOF packet parsed
// from an incoming datagram.
class ISofProcessor {
public:
    virtual ~ISofProcessor() = default;
    virtual void OnSof(const SofFields & f) = 0;
};

}  // namespace cigi_session
