#include "cigi_session/IgSession.h"

namespace cigi_session {

struct IgSession::Impl {};

IgSession::IgSession() : impl_(std::make_unique<Impl>()) {}
IgSession::~IgSession() = default;

void IgSession::SetIgCtrlProcessor(IIgCtrlProcessor *) {}
void IgSession::SetEntityCtrlProcessor(IEntityCtrlProcessor *) {}
void IgSession::SetHatHotReqProcessor(IHatHotReqProcessor *) {}
void IgSession::SetAtmosphereProcessor(IAtmosphereProcessor *) {}
void IgSession::SetEnvRegionProcessor(IEnvRegionProcessor *) {}
void IgSession::SetWeatherCtrlProcessor(IWeatherCtrlProcessor *) {}
void IgSession::SetCompCtrlProcessor(ICompCtrlProcessor *) {}

std::size_t IgSession::HandleDatagram(const std::uint8_t *, std::size_t) {
    return 0;
}

}  // namespace cigi_session
