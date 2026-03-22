#pragma once
#include <array>
#include <optional>
#include <cstdint>
#include <string>
#include <sim_msgs/msg/hat_hot_response.hpp>

// Circular buffer tracking in-flight CIGI HAT/HOT requests by request ID.
// Capacity = 32; oldest entry is overwritten when full.
class HatRequestTracker
{
public:
    static constexpr std::size_t CAPACITY = 32;

    void add_request(uint32_t request_id, double lat, double lon,
                     const std::string & point_name = "");

    // On receiving a HAT/HOT response from the IG, look up the original
    // request and assemble a HatHotResponse msg.  Returns nullopt if the
    // request_id is not found (stale or duplicate).
    std::optional<sim_msgs::msg::HatHotResponse>
    resolve(uint32_t request_id, double hot, bool valid);

    // Allocate a unique 16-bit request ID (wraps at 65535)
    uint16_t next_id() { return next_request_id_++; }

    // Discard all pending requests (e.g. on reposition — stale HOT is invalid)
    void clear() {
        for (auto & e : buf_) e.occupied = false;
    }

private:
    struct Entry {
        uint32_t request_id = 0;
        double   lat        = 0.0;
        double   lon        = 0.0;
        std::string point_name;
        bool     occupied   = false;
    };

    std::array<Entry, CAPACITY> buf_ {};
    std::size_t next_slot_ = 0;
    uint16_t next_request_id_ = 1;
};
