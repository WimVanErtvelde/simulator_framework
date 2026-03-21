#pragma once
// Minimal CIGI type aliases used by CIGI_IG_Interface vendored sources.
// On Linux these are just platform-native types; on Windows they map the same way.
#include <cstdint>

typedef uint8_t  Cigi_uint8;
typedef uint16_t Cigi_uint16;
typedef uint32_t Cigi_uint32;
typedef int8_t   Cigi_int8;

namespace CIGI_IG_Interface_NS
{
    // Terrain probe position (lat/lon degrees, alt metres MSL)
    struct Position {
        double lat;
        double lon;
        double alt;
    };

    // Extended terrain probe result
    struct ExtendedInfo {
        unsigned short material;
        float normalAz;
        float normalEl;
    };
}
