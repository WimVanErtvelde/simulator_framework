#include "cigi_session/HostSession.h"

#include <CigiHostSession.h>
#include <CigiOutgoingMsg.h>
#include <CigiIGCtrlV3_3.h>
#include <CigiBaseIGCtrl.h>
#include <CigiHatHotReqV3_2.h>
#include <CigiBaseHatHotReq.h>

namespace cigi_session {

struct HostSession::Impl {
    CigiHostSession ccl;
    Cigi_uint8 * last_msg = nullptr;
    int last_len = 0;

    Impl() {
        ccl.SetCigiVersion(3, 3);
    }
};

HostSession::HostSession() : impl_(std::make_unique<Impl>()) {}
HostSession::~HostSession() = default;

void HostSession::BeginFrame(std::uint32_t frame_cntr, std::uint8_t ig_mode,
                              double timestamp_s) {
    auto & out = impl_->ccl.GetOutgoingMsgMgr();
    out.BeginMsg();

    CigiIGCtrlV3_3 ig;
    ig.SetDatabaseID(0);
    ig.SetIGMode(static_cast<CigiBaseIGCtrl::IGModeGrp>(ig_mode));
    ig.SetTimeStampValid(true);
    ig.SetFrameCntr(frame_cntr);
    ig.SetTimeStamp(static_cast<Cigi_uint32>(timestamp_s * 1e6));  // µs
    out << ig;
}

void HostSession::AppendHatHotRequest(std::uint16_t request_id,
                                       double lat_deg, double lon_deg) {
    CigiHatHotReqV3_2 pkt;
    pkt.SetHatHotID(request_id);
    pkt.SetReqType(CigiBaseHatHotReq::Extended);
    pkt.SetSrcCoordSys(CigiBaseHatHotReq::Geodetic);
    pkt.SetUpdatePeriod(0);
    pkt.SetEntityID(0);
    pkt.SetLat(lat_deg);
    pkt.SetLon(lon_deg);
    pkt.SetAlt(0.0);
    impl_->ccl.GetOutgoingMsgMgr() << pkt;
}

std::pair<const std::uint8_t *, std::size_t> HostSession::FinishFrame() {
    auto & out = impl_->ccl.GetOutgoingMsgMgr();
    if (out.PackageMsg(&impl_->last_msg, impl_->last_len) != 0 ||
        impl_->last_msg == nullptr || impl_->last_len <= 0) {
        return {nullptr, 0};
    }
    return {impl_->last_msg, static_cast<std::size_t>(impl_->last_len)};
}

void HostSession::SetSofProcessor(ISofProcessor *) {}
void HostSession::SetHatHotRespProcessor(IHatHotRespProcessor *) {}

std::size_t HostSession::HandleDatagram(const std::uint8_t *, std::size_t) {
    return 0;
}

}  // namespace cigi_session
