#include "cigi_session/IgSession.h"

#include <CigiIGSession.h>
#include <CigiOutgoingMsg.h>
#include <CigiIncomingMsg.h>
#include <CigiSOFV3_2.h>
#include <CigiBaseSOF.h>
#include <CigiHatHotXRespV3_2.h>
#include <CigiBaseHatHotResp.h>

namespace cigi_session {

struct IgSession::Impl {
    CigiIGSession ccl;
    IIgCtrlProcessor *      ig_ctrl = nullptr;
    IEntityCtrlProcessor *  entity  = nullptr;
    IHatHotReqProcessor *   hat_hot = nullptr;
    IAtmosphereProcessor *  atmos   = nullptr;
    IEnvRegionProcessor *   env     = nullptr;
    IWeatherCtrlProcessor * wx      = nullptr;
    ICompCtrlProcessor *    comp    = nullptr;
    Cigi_uint8 *            last_msg = nullptr;
    int                     last_len = 0;

    Impl() {
        ccl.SetCigiVersion(3, 3);
        ccl.GetIncomingMsgMgr().SetReaderCigiVersion(3, 3);
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
