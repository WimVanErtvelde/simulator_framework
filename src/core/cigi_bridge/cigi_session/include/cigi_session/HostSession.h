// Host-side CIGI 3.3 session. Owns buffered wire output and dispatches
// received IG→Host packets (SOF, HAT/HOT Extended Response) to registered
// processors. Thin wrapper around CCL; app code never touches byte offsets.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace cigi_session {

class ISofProcessor;
class IHatHotRespProcessor;

class HostSession {
public:
    HostSession();
    ~HostSession();
    HostSession(const HostSession &) = delete;
    HostSession & operator=(const HostSession &) = delete;

    // Register processors. nullptr clears. Default is no-op.
    void SetSofProcessor(ISofProcessor * proc);
    void SetHatHotRespProcessor(IHatHotRespProcessor * proc);

    // Parse a received datagram, dispatching each packet to its processor.
    // Returns the number of packets successfully parsed.
    std::size_t HandleDatagram(const std::uint8_t * data, std::size_t len);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cigi_session
