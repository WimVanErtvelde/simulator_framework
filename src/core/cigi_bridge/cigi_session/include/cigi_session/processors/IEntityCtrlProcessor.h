#pragma once
#include <cstdint>

namespace cigi_session {

struct EntityCtrlFields {
    std::uint16_t entity_id;
    std::uint8_t  entity_state;       // 0=Inactive/Standby, 1=Active, 2=Destroyed
    bool          attached;
    std::uint8_t  alpha;
    float         roll_deg;
    float         pitch_deg;
    float         yaw_deg;
    double        lat_deg;
    double        lon_deg;
    double        alt_m;
};

// Inbound Entity Control dispatch (CIGI 3.3 §4.1.2, packet 0x02).
class IEntityCtrlProcessor {
public:
    virtual ~IEntityCtrlProcessor() = default;
    virtual void OnEntityCtrl(const EntityCtrlFields & f) = 0;
};

}  // namespace cigi_session
