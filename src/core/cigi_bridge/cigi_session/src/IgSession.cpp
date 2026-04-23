#include "cigi_session/IgSession.h"
#include "cigi_session/processors/IIgCtrlProcessor.h"
#include "cigi_session/processors/IHatHotReqProcessor.h"
#include "cigi_session/processors/IEntityCtrlProcessor.h"

#include <CigiIGSession.h>
#include <CigiOutgoingMsg.h>
#include <CigiIncomingMsg.h>
#include <CigiBaseEventProcessor.h>
#include <CigiSOFV3_2.h>
#include <CigiBaseSOF.h>
#include <CigiHatHotXRespV3_2.h>
#include <CigiBaseHatHotResp.h>
#include <CigiHatHotReqV3_2.h>
#include <CigiBaseHatHotReq.h>
#include <CigiEntityCtrlV3_3.h>
#include <CigiBaseEntityCtrl.h>
#include <CigiAnimationTable.h>
#include <CigiIGCtrlV3_3.h>
#include <CigiBaseIGCtrl.h>

namespace cigi_session {

namespace {

class IgCtrlAdapter : public CigiBaseEventProcessor {
public:
    explicit IgCtrlAdapter(IIgCtrlProcessor * const * t) : target_(t) {}
    void OnPacketReceived(CigiBasePacket * pkt) override {
        if (!*target_ || !pkt) return;
        auto * ig = dynamic_cast<CigiBaseIGCtrl *>(pkt);
        if (!ig) return;
        IgCtrlFields f;
        f.ig_mode              = static_cast<std::uint8_t>(ig->GetIGMode());
        f.database_id          = ig->GetDatabaseID();
        f.host_frame_number    = ig->GetFrameCntr();
        // TimeStamp accessor is V3-specific; guard dynamic_cast.
        f.timestamp_10us_ticks = 0;
        if (auto * ig33 = dynamic_cast<CigiIGCtrlV3_3 *>(ig)) {
            f.timestamp_10us_ticks = ig33->GetTimeStamp();
        }
        (*target_)->OnIgCtrl(f);
    }
private:
    IIgCtrlProcessor * const * target_;
};

class HatHotReqAdapter : public CigiBaseEventProcessor {
public:
    explicit HatHotReqAdapter(IHatHotReqProcessor * const * t) : target_(t) {}
    void OnPacketReceived(CigiBasePacket * pkt) override {
        if (!*target_ || !pkt) return;
        auto * req = dynamic_cast<CigiHatHotReqV3_2 *>(pkt);
        if (!req) return;
        HatHotReqFields f;
        f.request_id    = req->GetHatHotID();
        f.extended      = (req->GetReqType() == CigiBaseHatHotReq::Extended);
        f.geodetic      = (req->GetSrcCoordSys() == CigiBaseHatHotReq::Geodetic);
        f.update_period = req->GetUpdatePeriod();
        f.entity_id     = req->GetEntityID();
        f.lat_deg       = req->GetLat();
        f.lon_deg       = req->GetLon();
        f.alt_m         = req->GetAlt();
        (*target_)->OnHatHotReq(f);
    }
private:
    IHatHotReqProcessor * const * target_;
};

class EntityCtrlAdapter : public CigiBaseEventProcessor {
public:
    explicit EntityCtrlAdapter(IEntityCtrlProcessor * const * t) : target_(t) {}
    void OnPacketReceived(CigiBasePacket * pkt) override {
        if (!*target_ || !pkt) return;
        auto * e = dynamic_cast<CigiEntityCtrlV3_3 *>(pkt);
        if (!e) return;
        EntityCtrlFields f;
        f.entity_id    = e->GetEntityID();
        f.entity_state = static_cast<std::uint8_t>(e->GetEntityState());
        f.attached     = (e->GetAttachState() == CigiBaseEntityCtrl::Attach);
        f.alpha        = e->GetAlpha();
        f.roll_deg     = e->GetRoll();
        f.pitch_deg    = e->GetPitch();
        f.yaw_deg      = e->GetYaw();
        f.lat_deg      = e->GetLat();
        f.lon_deg      = e->GetLon();
        f.alt_m        = e->GetAlt();
        (*target_)->OnEntityCtrl(f);
    }
private:
    IEntityCtrlProcessor * const * target_;
};

}  // namespace

struct IgSession::Impl {
    CigiIGSession ccl;
    IIgCtrlProcessor *      ig_ctrl = nullptr;
    IEntityCtrlProcessor *  entity  = nullptr;
    IHatHotReqProcessor *   hat_hot = nullptr;
    IAtmosphereProcessor *  atmos   = nullptr;
    IEnvRegionProcessor *   env     = nullptr;
    IWeatherCtrlProcessor * wx      = nullptr;
    ICompCtrlProcessor *    comp    = nullptr;
    IgCtrlAdapter           ig_ctrl_adapter;
    HatHotReqAdapter        hat_hot_adapter;
    EntityCtrlAdapter       entity_adapter;
    CigiAnimationTable      animation_table;
    Cigi_uint8 *            last_msg = nullptr;
    int                     last_len = 0;

    Impl()
      : ig_ctrl_adapter(&ig_ctrl),
        hat_hot_adapter(&hat_hot),
        entity_adapter(&entity) {
        ccl.SetCigiVersion(3, 3);
        ccl.GetIncomingMsgMgr().SetReaderCigiVersion(3, 3);
        // Entity Control's Unpack takes a CigiAnimationTable spec pointer;
        // store ours and register it via RegisterUserPacket... but for this
        // read-only case the default table works.
        ccl.GetIncomingMsgMgr().RegisterEventProcessor(
            CIGI_IG_CTRL_PACKET_ID_V3, &ig_ctrl_adapter);
        ccl.GetIncomingMsgMgr().RegisterEventProcessor(
            CIGI_HAT_HOT_REQ_PACKET_ID_V3, &hat_hot_adapter);
        ccl.GetIncomingMsgMgr().RegisterEventProcessor(
            CIGI_ENTITY_CTRL_PACKET_ID_V3, &entity_adapter);
    }
};

IgSession::IgSession() : impl_(std::make_unique<Impl>()) {}
IgSession::~IgSession() = default;

void IgSession::BeginFrame(std::uint8_t ig_mode,
                            std::int8_t database_id,
                            std::uint32_t ig_frame_number,
                            std::uint32_t last_host_frame) {
    auto & out = impl_->ccl.GetOutgoingMsgMgr();
    out.BeginMsg();

    CigiSOFV3_2 sof;
    sof.SetIGMode(static_cast<CigiBaseSOF::IGModeGrp>(ig_mode));
    sof.SetDatabaseID(database_id);
    sof.SetTimeStampValid(false);
    sof.SetEarthRefModel(CigiBaseSOF::WGS84);
    sof.SetFrameCntr(ig_frame_number);
    sof.SetLastRcvdHostFrame(last_host_frame);
    out << sof;
}

void IgSession::AppendHatHotXResp(std::uint16_t request_id,
                                    bool valid,
                                    double hat_m, double hot_m,
                                    std::uint32_t material_code,
                                    float normal_azimuth_deg,
                                    float normal_elevation_deg) {
    CigiHatHotXRespV3_2 pkt;
    pkt.SetHatHotID(request_id);
    pkt.SetValid(valid);
    pkt.SetHat(hat_m);
    pkt.SetHot(hot_m);
    pkt.SetMaterial(material_code);
    pkt.SetNormAz(normal_azimuth_deg);
    pkt.SetNormEl(normal_elevation_deg);
    impl_->ccl.GetOutgoingMsgMgr() << pkt;
}

std::pair<const std::uint8_t *, std::size_t> IgSession::FinishFrame() {
    auto & out = impl_->ccl.GetOutgoingMsgMgr();
    if (out.PackageMsg(&impl_->last_msg, impl_->last_len) != 0 ||
        impl_->last_msg == nullptr || impl_->last_len <= 0) {
        return {nullptr, 0};
    }
    return {impl_->last_msg, static_cast<std::size_t>(impl_->last_len)};
}

void IgSession::SetIgCtrlProcessor(IIgCtrlProcessor * p)     { impl_->ig_ctrl = p; }
void IgSession::SetEntityCtrlProcessor(IEntityCtrlProcessor * p) { impl_->entity = p; }
void IgSession::SetHatHotReqProcessor(IHatHotReqProcessor * p)   { impl_->hat_hot = p; }
void IgSession::SetAtmosphereProcessor(IAtmosphereProcessor * p) { impl_->atmos = p; }
void IgSession::SetEnvRegionProcessor(IEnvRegionProcessor * p)   { impl_->env = p; }
void IgSession::SetWeatherCtrlProcessor(IWeatherCtrlProcessor * p) { impl_->wx = p; }
void IgSession::SetCompCtrlProcessor(ICompCtrlProcessor * p)     { impl_->comp = p; }

std::size_t IgSession::HandleDatagram(const std::uint8_t * data, std::size_t len) {
    if (data == nullptr || len == 0) return 0;
    impl_->ccl.GetIncomingMsgMgr().ProcessIncomingMsg(
        const_cast<Cigi_uint8 *>(data), static_cast<int>(len));
    return 1;
}

}  // namespace cigi_session
