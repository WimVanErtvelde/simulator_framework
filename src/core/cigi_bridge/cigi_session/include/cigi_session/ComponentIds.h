#pragma once
#include <cstdint>

// Framework-defined Component IDs for Component Control (CIGI 3.3 §4.1.4).
// IDs are namespaced per Component Class. CIGI 3.3 leaves Component IDs
// entirely IG-defined per class; the framework reserves its own ranges here.
namespace cigi_session {

// ComponentClass::GlobalTerrainSurface (8) — terrain-surface overrides.
// IDs 100..199 reserved for framework terrain overrides.
enum class GlobalTerrainComponentId : std::uint16_t {
    RunwayFriction = 100,   // Component State = friction level (0..15)
    // 101-199 reserved for future terrain overrides.
};

// ComponentClass::System (13) — IG-wide system behaviour flags. Per §4.1.4
// Table 9, the System class has no Instance ID and is intended for signals
// that apply globally to the IG. IDs 200..299 reserved for framework
// system-level flags.
enum class SystemComponentId : std::uint16_t {
    SimFreezeState = 200,   // Component State: 0 = RUNNING, 1 = FROZEN.
                            // Emitted every frame (idempotent) to survive
                            // UDP packet loss.
    // 201-299 reserved for future system-level flags.
};

}  // namespace cigi_session
