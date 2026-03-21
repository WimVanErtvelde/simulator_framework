#include "cigi_bridge/hat_request_tracker.hpp"

void HatRequestTracker::add_request(uint32_t request_id, double lat, double lon,
                                    const std::string & point_name)
{
    buf_[next_slot_] = {request_id, lat, lon, point_name, true};
    next_slot_ = (next_slot_ + 1) % CAPACITY;
}

std::optional<sim_msgs::msg::HatHotResponse>
HatRequestTracker::resolve(uint32_t request_id, double hot, bool valid)
{
    for (auto & e : buf_) {
        if (!e.occupied || e.request_id != request_id) continue;

        e.occupied = false;
        sim_msgs::msg::HatHotResponse resp;
        resp.request_id = request_id;
        resp.point_name = e.point_name;
        resp.lat        = e.lat;
        resp.lon        = e.lon;
        resp.hot        = hot;
        resp.hat        = 0.0;   // HAT needs current aircraft alt — caller fills if needed
        resp.valid      = valid;
        return resp;
    }
    return std::nullopt;
}
