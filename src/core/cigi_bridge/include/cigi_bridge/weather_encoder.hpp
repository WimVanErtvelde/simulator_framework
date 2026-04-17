#ifndef CIGI_BRIDGE__WEATHER_ENCODER_HPP_
#define CIGI_BRIDGE__WEATHER_ENCODER_HPP_

#include <sim_msgs/msg/weather_state.hpp>
#include <vector>
#include <cstdint>

namespace cigi_bridge
{

/// Encode CIGI 3.3 weather packets from WeatherState into the provided buffer.
/// Returns number of bytes written. Buffer must have sufficient capacity.
/// Appends: one Atmosphere Control (32B) + N Weather Control packets (56B each).
size_t encode_weather_packets(
    uint8_t * buffer,
    size_t buffer_capacity,
    const sim_msgs::msg::WeatherState & weather);

}  // namespace cigi_bridge

#endif  // CIGI_BRIDGE__WEATHER_ENCODER_HPP_
