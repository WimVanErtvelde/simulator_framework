#pragma once
#include <cstdint>

namespace cigi_session {

struct IgCtrlFields {
    std::uint8_t  ig_mode;              // 0=Standby/Reset,1=Operate,2=Debug,3=Offline
    std::int8_t   database_id;
    std::uint32_t host_frame_number;
    std::uint32_t timestamp_10us_ticks;
};

// Inbound IG Control dispatch (CIGI 3.3 §4.1.1, packet 0x01).
class IIgCtrlProcessor {
public:
    virtual ~IIgCtrlProcessor() = default;
    virtual void OnIgCtrl(const IgCtrlFields & f) = 0;
};

}  // namespace cigi_session
