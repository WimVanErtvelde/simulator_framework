#pragma once
#include <cstdint>

namespace cigi_session {

struct HatHotRespFields {
    std::uint16_t request_id;
    bool          valid;
    double        hat_m;
    double        hot_m;
    std::uint32_t material_code;
    float         normal_azimuth_deg;
    float         normal_elevation_deg;
};

// Inbound HAT/HOT Extended Response dispatch (CIGI 3.3 §4.2.3, packet 0x67).
class IHatHotRespProcessor {
public:
    virtual ~IHatHotRespProcessor() = default;
    virtual void OnHatHotResp(const HatHotRespFields & f) = 0;
};

}  // namespace cigi_session
