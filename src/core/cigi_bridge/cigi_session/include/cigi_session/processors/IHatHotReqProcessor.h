#pragma once
#include <cstdint>

namespace cigi_session {

struct HatHotReqFields {
    std::uint16_t request_id;
    bool          extended;       // true = Extended (→ 0x67 XResp), false = basic
    bool          geodetic;       // true = Geodetic, false = Entity-relative
    std::uint8_t  update_period;  // 0 = one-shot, N = every N frames
    std::uint16_t entity_id;      // only meaningful when !geodetic
    double        lat_deg;
    double        lon_deg;
    double        alt_m;
};

// Inbound HAT/HOT Request dispatch (CIGI 3.3 §4.1.24, packet 0x18).
class IHatHotReqProcessor {
public:
    virtual ~IHatHotReqProcessor() = default;
    virtual void OnHatHotReq(const HatHotReqFields & f) = 0;
};

}  // namespace cigi_session
