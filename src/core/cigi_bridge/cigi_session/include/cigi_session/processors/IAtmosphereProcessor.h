#pragma once
#include <cstdint>

namespace cigi_session {

struct AtmosphereRxFields {
    bool          atmos_enable;
    std::uint8_t  humidity_pct;
    float         temperature_c;
    float         visibility_m;
    float         horiz_wind_ms;
    float         vert_wind_ms;
    float         wind_direction_deg;
    float         barometric_pressure_hpa;
};

// Inbound Atmosphere Control dispatch (CIGI 3.3 §4.1.10, packet 0x0A).
class IAtmosphereProcessor {
public:
    virtual ~IAtmosphereProcessor() = default;
    virtual void OnAtmosphere(const AtmosphereRxFields & f) = 0;
};

}  // namespace cigi_session
