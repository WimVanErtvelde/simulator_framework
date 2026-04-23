#pragma once
#include <cstdint>

namespace cigi_session {

struct CompCtrlFields {
    std::uint8_t  component_class;   // 0..15; 8 = GlobalTerrainSurface etc.
    std::uint16_t instance_id;
    std::uint16_t component_id;
    std::uint8_t  component_state;
    std::uint32_t data[6];
};

// Inbound Component Control dispatch (CIGI 3.3 §4.1.4, packet 0x04).
class ICompCtrlProcessor {
public:
    virtual ~ICompCtrlProcessor() = default;
    virtual void OnCompCtrl(const CompCtrlFields & f) = 0;
};

}  // namespace cigi_session
