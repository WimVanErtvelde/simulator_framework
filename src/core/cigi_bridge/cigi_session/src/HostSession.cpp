#include "cigi_session/HostSession.h"

namespace cigi_session {

struct HostSession::Impl {};

HostSession::HostSession() : impl_(std::make_unique<Impl>()) {}
HostSession::~HostSession() = default;

void HostSession::SetSofProcessor(ISofProcessor *) {}
void HostSession::SetHatHotRespProcessor(IHatHotRespProcessor *) {}

std::size_t HostSession::HandleDatagram(const std::uint8_t *, std::size_t) {
    return 0;
}

}  // namespace cigi_session
