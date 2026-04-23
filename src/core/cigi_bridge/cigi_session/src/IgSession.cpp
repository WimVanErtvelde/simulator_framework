#include "cigi_session/IgSession.h"
#include "cigi_session/processors/IIgCtrlProcessor.h"
#include "cigi_session/processors/IHatHotReqProcessor.h"
#include "cigi_session/processors/IEntityCtrlProcessor.h"
#include "cigi_session/processors/IAtmosphereProcessor.h"
#include "cigi_session/processors/IEnvRegionProcessor.h"
#include "cigi_session/processors/IWeatherCtrlProcessor.h"
#include "cigi_session/processors/ICompCtrlProcessor.h"

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
#include <CigiAtmosCtrl.h>
#include <CigiBaseEnvCtrl.h>
#include <CigiEnvRgnCtrlV3.h>
#include <CigiBaseEnvRgnCtrl.h>
#include <CigiWeatherCtrlV3.h>
#include <CigiBaseWeatherCtrl.h>
#include <CigiCompCtrlV3_3.h>
#include <CigiBaseCompCtrl.h>
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

class AtmosphereAdapter : public CigiBaseEventProcessor {
public:
    explicit AtmosphereAdapter(IAtmosphereProcessor * const * t) : target_(t) {}
    void OnPacketReceived(CigiBasePacket * pkt) override {
        if (!*target_ || !pkt) return;
        auto * a = dynamic_cast<CigiAtmosCtrlV3 *>(pkt);
        if (!a) return;
        AtmosphereRxFields f;
        f.atmos_enable            = a->GetAtmosEn();
        f.humidity_pct            = a->GetHumidity();
        f.temperature_c           = a->GetAirTemp();
        f.visibility_m            = a->GetVisibility();
        f.horiz_wind_ms           = a->GetHorizWindSp();
        f.vert_wind_ms            = a->GetVertWindSp();
        f.wind_direction_deg      = a->GetWindDir();
        f.barometric_pressure_hpa = a->GetBaroPress();
        (*target_)->OnAtmosphere(f);
    }
private:
    IAtmosphereProcessor * const * target_;
};

class EnvRegionAdapter : public CigiBaseEventProcessor {
public:
    explicit EnvRegionAdapter(IEnvRegionProcessor * const * t) : target_(t) {}
    void OnPacketReceived(CigiBasePacket * pkt) override {
        if (!*target_ || !pkt) return;
        auto * r = dynamic_cast<CigiEnvRgnCtrlV3 *>(pkt);
        if (!r) return;
        EnvRegionFields f;
        f.region_id              = r->GetRegionID();
        f.region_state           = static_cast<std::uint8_t>(r->GetRgnState());
        f.merge_weather          = (r->GetWeatherProp() == CigiBaseEnvRgnCtrl::Merge);
        f.lat_deg                = r->GetLat();
        f.lon_deg                = r->GetLon();
        f.size_x_m               = r->GetXSize();
        f.size_y_m               = r->GetYSize();
        f.corner_radius_m        = r->GetCornerRadius();
        f.rotation_deg           = r->GetRotation();
        f.transition_perimeter_m = r->GetTransition();
        (*target_)->OnEnvRegion(f);
    }
private:
    IEnvRegionProcessor * const * target_;
};

class WeatherCtrlAdapter : public CigiBaseEventProcessor {
public:
    explicit WeatherCtrlAdapter(IWeatherCtrlProcessor * const * t) : target_(t) {}
    void OnPacketReceived(CigiBasePacket * pkt) override {
        if (!*target_ || !pkt) return;
        auto * w = dynamic_cast<CigiWeatherCtrlV3 *>(pkt);
        if (!w) return;
        WeatherCtrlRxFields f;
        f.region_id                 = w->GetRegionID();
        f.layer_id                  = w->GetLayerID();
        f.humidity_pct              = w->GetHumidity();
        f.weather_enable            = w->GetWeatherEn();
        f.scud_enable               = w->GetScudEn();
        f.cloud_type                = static_cast<std::uint8_t>(w->GetCloudType());
        f.scope                     = static_cast<std::uint8_t>(w->GetScope());
        f.severity                  = w->GetSeverity();
        f.air_temp_c                = w->GetAirTemp();
        f.visibility_m              = w->GetVisibilityRng();
        f.scud_frequency_pct        = w->GetScudFreq();
        f.coverage_pct              = w->GetCoverage();
        f.base_elevation_m          = w->GetBaseElev();
        f.thickness_m               = w->GetThickness();
        f.transition_band_m         = w->GetTransition();
        f.horiz_wind_ms             = w->GetHorizWindSp();
        f.vert_wind_ms              = w->GetVertWindSp();
        f.wind_direction_deg        = w->GetWindDir();
        f.barometric_pressure_hpa   = w->GetBaroPress();
        f.aerosol_concentration_gm3 = w->GetAerosol();
        (*target_)->OnWeatherCtrl(f);
    }
private:
    IWeatherCtrlProcessor * const * target_;
};

class CompCtrlAdapter : public CigiBaseEventProcessor {
public:
    explicit CompCtrlAdapter(ICompCtrlProcessor * const * t) : target_(t) {}
    void OnPacketReceived(CigiBasePacket * pkt) override {
        if (!*target_ || !pkt) return;
        auto * c = dynamic_cast<CigiCompCtrlV3_3 *>(pkt);
        if (!c) return;
        CompCtrlFields f{};
        f.component_class = static_cast<std::uint8_t>(c->GetCompClassV3());
        f.instance_id     = c->GetInstanceID();
        f.component_id    = c->GetCompID();
        f.component_state = c->GetCompState();
        for (int i = 0; i < 6; ++i) {
            f.data[i] = c->GetLongCompData(i);
        }
        (*target_)->OnCompCtrl(f);
    }
private:
    ICompCtrlProcessor * const * target_;
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
    AtmosphereAdapter       atmos_adapter;
    EnvRegionAdapter        env_adapter;
    WeatherCtrlAdapter      wx_adapter;
    CompCtrlAdapter         comp_adapter;
    CigiAnimationTable      animation_table;
    Cigi_uint8 *            last_msg = nullptr;
    int                     last_len = 0;

    Impl()
      : ig_ctrl_adapter(&ig_ctrl),
        hat_hot_adapter(&hat_hot),
        entity_adapter(&entity),
        atmos_adapter(&atmos),
        env_adapter(&env),
        wx_adapter(&wx),
        comp_adapter(&comp) {
        ccl.SetCigiVersion(3, 3);
        ccl.GetIncomingMsgMgr().SetReaderCigiVersion(3, 3);
        auto & in = ccl.GetIncomingMsgMgr();
        in.RegisterEventProcessor(CIGI_IG_CTRL_PACKET_ID_V3,      &ig_ctrl_adapter);
        in.RegisterEventProcessor(CIGI_HAT_HOT_REQ_PACKET_ID_V3,  &hat_hot_adapter);
        in.RegisterEventProcessor(CIGI_ENTITY_CTRL_PACKET_ID_V3,  &entity_adapter);
        in.RegisterEventProcessor(CIGI_ATMOS_CTRL_PACKET_ID_V3,   &atmos_adapter);
        in.RegisterEventProcessor(CIGI_ENV_RGN_CTRL_PACKET_ID_V3, &env_adapter);
        in.RegisterEventProcessor(CIGI_WEATHER_CTRL_PACKET_ID_V3, &wx_adapter);
        in.RegisterEventProcessor(CIGI_COMP_CTRL_PACKET_ID_V3,    &comp_adapter);
    }
};

IgSession::IgSession() : impl_(std::make_unique<Impl>()) {}
IgSession::~IgSession() = default;

void IgSession::BeginFrame(std::uint8_t ig_mode,
                            std::int8_t database_id,
                            std::uint32_t ig_frame_number,
                            std::uint32_t last_host_frame) {
    auto & out = impl_->ccl.GetOutgoingMsgMgr();
    if (impl_->last_msg) {
        out.FreeMsg();
        impl_->last_msg = nullptr;
        impl_->last_len = 0;
    }
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
    // CCL throws CigiMissingIgControlException / CigiBufferOverrunException
    // on malformed datagrams; swallow so the caller's plugin/node process
    // is not torn down by a single bad packet.
    try {
        impl_->ccl.GetIncomingMsgMgr().ProcessIncomingMsg(
            const_cast<Cigi_uint8 *>(data), static_cast<int>(len));
    } catch (...) {
        return 0;
    }
    return 1;
}

}  // namespace cigi_session
