#pragma once
#include <cstdint>

// Framework-defined Component IDs for Component Control (CIGI 3.3 §4.1.4).
// IDs are namespaced per Component Class; IDs 100..199 under the
// GlobalTerrainSurface class are reserved for this framework's terrain
// overrides.
namespace cigi_session {

enum class GlobalTerrainComponentId : std::uint16_t {
    RunwayFriction = 100,   // Component State = friction level (0..15)
    // 101-199 reserved for future terrain overrides.
};

}  // namespace cigi_session
