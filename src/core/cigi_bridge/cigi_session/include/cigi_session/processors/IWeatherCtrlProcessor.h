#pragma once
#include <cstdint>

namespace cigi_session {

struct WeatherCtrlRxFields {
    std::uint16_t region_id;           // 0 when scope == Global
    std::uint8_t  layer_id;
    std::uint8_t  humidity_pct;
    bool          weather_enable;
    bool          scud_enable;
    std::uint8_t  cloud_type;          // CigiBaseWeatherCtrl::CloudTypeGrp
    std::uint8_t  scope;               // 0=Global, 1=Regional, 2=Entity
    std::uint8_t  severity;
    float         air_temp_c;
    float         visibility_m;
    float         scud_frequency_pct;
    float         coverage_pct;
    float         base_elevation_m;
    float         thickness_m;
    float         transition_band_m;
    float         horiz_wind_ms;
    float         vert_wind_ms;
    float         wind_direction_deg;
    float         barometric_pressure_hpa;
    float         aerosol_concentration_gm3;
};

// Inbound Weather Control dispatch (CIGI 3.3 §4.1.8, packet 0x08).
class IWeatherCtrlProcessor {
public:
    virtual ~IWeatherCtrlProcessor() = default;
    virtual void OnWeatherCtrl(const WeatherCtrlRxFields & f) = 0;
};

}  // namespace cigi_session
