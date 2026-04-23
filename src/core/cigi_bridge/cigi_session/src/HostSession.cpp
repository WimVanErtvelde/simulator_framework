#include "cigi_session/HostSession.h"
#include "cigi_session/processors/ISofProcessor.h"
#include "cigi_session/processors/IHatHotRespProcessor.h"

#include <CigiHostSession.h>
#include <CigiOutgoingMsg.h>
#include <CigiIncomingMsg.h>
#include <CigiBaseEventProcessor.h>
#include <CigiIGCtrlV3_3.h>
#include <CigiBaseIGCtrl.h>
#include <CigiEntityCtrlV3_3.h>
#include <CigiBaseEntityCtrl.h>
#include <CigiHatHotReqV3_2.h>
#include <CigiBaseHatHotReq.h>
#include <CigiSOFV3_2.h>
#include <CigiBaseSOF.h>
#include <CigiHatHotXRespV3_2.h>
#include <CigiBaseHatHotResp.h>

namespace cigi_session {

namespace {

// CigiBaseEventProcessor adapter: translates CCL's OnPacketReceived
// callback into an ISofProcessor::OnSof call. The outer HostSession owns the
// ISofProcessor pointer; the adapter dereferences it each dispatch so
// SetSofProcessor() can swap targets without re-registering.
class SofAdapter : public CigiBaseEventProcessor {
public:
    explicit SofAdapter(ISofProcessor * const * target) : target_(target) {}
    void OnPacketReceived(CigiBasePacket * pkt) override {
        if (!*target_ || !pkt) return;
        auto * sof = dynamic_cast<CigiBaseSOF *>(pkt);
        if (!sof) return;
        SofFields f;
        f.ig_mode                = static_cast<IgModeRx>(sof->GetIGMode());
        f.ig_status_code         = sof->GetIGStatus();
        f.database_id            = sof->GetDatabaseID();
        f.ig_frame_number        = sof->GetFrameCntr();
        f.last_host_frame_number = 0;  // filled by V3_2-specific accessor below
        // V3_2 adds LastRcvdHostFrame; safe to downcast here because
        // CigiIncomingMsg hands us the V3_2 converter for SOF.
        if (auto * sof32 = dynamic_cast<CigiSOFV3_2 *>(sof)) {
            f.last_host_frame_number = sof32->GetLastRcvdHostFrame();
        }
        (*target_)->OnSof(f);
    }
private:
    ISofProcessor * const * target_;
};

class HatHotRespAdapter : public CigiBaseEventProcessor {
public:
    explicit HatHotRespAdapter(IHatHotRespProcessor * const * target)
      : target_(target) {}
    void OnPacketReceived(CigiBasePacket * pkt) override {
        if (!*target_ || !pkt) return;
        auto * resp = dynamic_cast<CigiHatHotXRespV3_2 *>(pkt);
        if (!resp) return;
        HatHotRespFields f;
        f.request_id           = resp->GetHatHotID();
        f.valid                = resp->GetValid();
        f.hat_m                = resp->GetHat();
        f.hot_m                = resp->GetHot();
        f.material_code        = resp->GetMaterial();
        f.normal_azimuth_deg   = resp->GetNormAz();
        f.normal_elevation_deg = resp->GetNormEl();
        (*target_)->OnHatHotResp(f);
    }
private:
    IHatHotRespProcessor * const * target_;
};

}  // namespace

struct HostSession::Impl {
    CigiHostSession ccl;
    ISofProcessor *       sof_proc  = nullptr;
    IHatHotRespProcessor * resp_proc = nullptr;
    SofAdapter            sof_adapter;
    HatHotRespAdapter     resp_adapter;
    Cigi_uint8 *          last_msg = nullptr;
    int                   last_len = 0;

    Impl()
      : sof_adapter(&sof_proc),
        resp_adapter(&resp_proc) {
        ccl.SetCigiVersion(3, 3);
        ccl.GetIncomingMsgMgr().SetReaderCigiVersion(3, 3);
        ccl.GetIncomingMsgMgr().RegisterEventProcessor(
            CIGI_SOF_PACKET_ID_V3, &sof_adapter);
        ccl.GetIncomingMsgMgr().RegisterEventProcessor(
            CIGI_HAT_HOT_XRESP_PACKET_ID_V3, &resp_adapter);
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

void HostSession::AppendEntityCtrl(std::uint16_t entity_id,
                                     float roll_deg, float pitch_deg, float yaw_deg,
                                     double lat_deg, double lon_deg, double alt_m) {
    CigiEntityCtrlV3_3 pkt;
    pkt.SetEntityID(entity_id);
    pkt.SetEntityState(CigiBaseEntityCtrl::Active);
    pkt.SetAttachState(CigiBaseEntityCtrl::Detach);
    pkt.SetAlpha(255);
    pkt.SetRoll(roll_deg);
    pkt.SetPitch(pitch_deg);
    pkt.SetYaw(yaw_deg);
    pkt.SetLat(lat_deg);
    pkt.SetLon(lon_deg);
    pkt.SetAlt(alt_m);
    impl_->ccl.GetOutgoingMsgMgr() << pkt;
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

void HostSession::SetSofProcessor(ISofProcessor * p) { impl_->sof_proc = p; }
void HostSession::SetHatHotRespProcessor(IHatHotRespProcessor * p) { impl_->resp_proc = p; }

std::size_t HostSession::HandleDatagram(const std::uint8_t * data, std::size_t len) {
    if (data == nullptr || len == 0) return 0;
    auto & in = impl_->ccl.GetIncomingMsgMgr();
    // CCL auto-detects byte order from the Byte Swap Magic in SOF/IGCtrl
    // (bytes 6-7) so the same parser handles native-LE and swapped-BE
    // datagrams transparently.
    in.ProcessIncomingMsg(const_cast<Cigi_uint8 *>(data), static_cast<int>(len));
    return 1;  // CCL doesn't report per-packet counts via this entry point
}

}  // namespace cigi_session
