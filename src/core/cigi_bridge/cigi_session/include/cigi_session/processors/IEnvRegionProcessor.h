#pragma once
#include <cstdint>

namespace cigi_session {

struct EnvRegionFields {
    std::uint16_t region_id;
    std::uint8_t  region_state;   // 0=Inactive, 1=Active, 2=Destroyed
    bool          merge_weather;
    double        lat_deg;
    double        lon_deg;
    float         size_x_m;
    float         size_y_m;
    float         corner_radius_m;
    float         rotation_deg;
    float         transition_perimeter_m;
};

// Inbound Environmental Region Control dispatch (CIGI 3.3 §4.1.9, packet 0x09).
class IEnvRegionProcessor {
public:
    virtual ~IEnvRegionProcessor() = default;
    virtual void OnEnvRegion(const EnvRegionFields & f) = 0;
};

}  // namespace cigi_session
