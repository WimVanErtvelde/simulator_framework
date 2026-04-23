#include <gtest/gtest.h>

#include "cigi_session/HostSession.h"
#include "cigi_session/IgSession.h"
#include "cigi_session/ComponentIds.h"

#include <CigiAtmosCtrl.h>
#include <CigiWeatherCtrlV3.h>
#include <CigiBaseWeatherCtrl.h>
#include <CigiEnvRgnCtrlV3.h>
#include <CigiBaseEnvRgnCtrl.h>
#include <CigiCompCtrlV3_3.h>
#include <CigiBaseCompCtrl.h>
#include <CigiSOFV3_2.h>
#include <CigiBaseSOF.h>
#include <CigiHatHotXRespV3_2.h>
#include <CigiIGCtrlV3_3.h>
#include <CigiBaseIGCtrl.h>
#include <CigiEntityCtrlV3_3.h>
#include <CigiBaseEntityCtrl.h>
#include <CigiAnimationTable.h>
#include <CigiHatHotReqV3_2.h>
#include <CigiBaseHatHotReq.h>

#include "cigi_session/processors/ISofProcessor.h"
#include "cigi_session/processors/IHatHotRespProcessor.h"
#include "cigi_session/processors/IIgCtrlProcessor.h"
#include <cstring>
#include <optional>

// CCL writes multi-byte fields in the sender's native byte order and embeds a
// Byte Swap Magic (0x8000) in IG Control / SOF so recipients of a different
// endianness can detect the need to swap. When the recipient has the same
// endianness as the sender (this test: host-host on the same machine),
// Unpack's Swap flag is false. This differs from the retired
// test_cigi_wire_conformance which parsed bytes produced by our old
// hand-rolled BE encoders (Swap=true on LE hosts).
constexpr bool kSameEndianSwap = false;

// Every Host→IG datagram starts with an IG Control (24 bytes in CIGI 3.3).
constexpr std::size_t kIgCtrlSize = 24;

TEST(CigiSession, BeginFrameEmitsIgControl) {
    cigi_session::HostSession sess;
    sess.BeginFrame(/*frame_cntr=*/7, /*ig_mode=*/1, /*ts=*/0.0);
    auto [buf, len] = sess.FinishFrame();
    ASSERT_NE(buf, nullptr);
    ASSERT_GE(len, kIgCtrlSize);

    CigiIGCtrlV3_3 cigi;
    ASSERT_GE(cigi.Unpack(const_cast<std::uint8_t *>(buf), kSameEndianSwap, nullptr), 0);
    EXPECT_EQ(cigi.GetIGMode(), CigiBaseIGCtrl::Operate);
    EXPECT_EQ(cigi.GetFrameCntr(), 7u);
}

TEST(CigiSession, EntityCtrlRoundTrip) {
    cigi_session::HostSession sess;
    sess.BeginFrame(1, 1, 0.0);
    sess.AppendEntityCtrl(/*id=*/0, -1.5f, 2.5f, 123.4f,
                          50.901389, 4.484444, 56.0);
    auto [buf, len] = sess.FinishFrame();
    ASSERT_NE(buf, nullptr);
    ASSERT_GE(len, kIgCtrlSize + 48u);

    const std::uint8_t * ent_bytes = buf + kIgCtrlSize;

    CigiEntityCtrlV3_3 cigi;
    CigiAnimationTable tbl;
    ASSERT_GE(cigi.Unpack(const_cast<std::uint8_t *>(ent_bytes), kSameEndianSwap, &tbl), 0);

    EXPECT_EQ(cigi.GetEntityID(), 0);
    EXPECT_EQ(cigi.GetEntityState(), CigiBaseEntityCtrl::Active);
    EXPECT_FLOAT_EQ(cigi.GetRoll(), -1.5f);
    EXPECT_FLOAT_EQ(cigi.GetPitch(), 2.5f);
    EXPECT_FLOAT_EQ(cigi.GetYaw(), 123.4f);
    EXPECT_DOUBLE_EQ(cigi.GetLat(), 50.901389);
    EXPECT_DOUBLE_EQ(cigi.GetLon(), 4.484444);
    EXPECT_DOUBLE_EQ(cigi.GetAlt(), 56.0);
}

namespace {
struct SofSpy : cigi_session::ISofProcessor {
    std::optional<cigi_session::SofFields> got;
    void OnSof(const cigi_session::SofFields & f) override { got = f; }
};
}  // namespace

// End-to-end inbound round-trip: a host session emits a frame headed by IG
// Control and containing a synthetic SOF, then another HostSession ingests
// that datagram. The receiver's SofSpy gets the original frame counter and
// mode back. This covers (1) our ISofProcessor adapter wiring, (2) CCL's
// byte-swap auto-detection via Byte Swap Magic, and (3) the end-to-end
// session contract HostSession.HandleDatagram must uphold.
//
// IG sessions are covered separately in Task 3.2+; here we bypass them and
// drive CigiHostSession through a hand-built wire buffer.
TEST(CigiSession, SofProcessorDispatchesToTarget) {
    // CIGI 3.3 §4.2.1 SOF is 24 bytes. Build a native-byte-order buffer by
    // going through CigiOutgoingMsg indirectly — the simplest route is to
    // Pack a SOF via a throwaway IG session. But ProcessIncomingMsg expects
    // IG Control OR SOF as first packet; SOF is right for host-side receive.
    // Use CCL's OutgoingMsg directly by packaging a SOF; that flow is
    // exercised in detail in Task 3.2, so for this library test we skip the
    // IG session detail and build the SOF bytes by hand from the spec.
    std::uint8_t buf[24] = {};
    buf[0] = 0x65;               // Packet ID = 101 (SOF)
    buf[1] = 24;                 // Packet Size
    buf[2] = 3;                  // CIGI Major Version
    buf[3] = 0;                  // Database ID
    buf[4] = 0;                  // IG Status
    buf[5] = (2u << 4) | (1u << 2) | 1u;  // Minor=2, TSValid=1, IGMode=Operate(1)
    buf[6] = 0x80;               // Byte Swap Magic high byte (0x8000 BE)
    buf[7] = 0x00;
    // IG Frame Counter big-endian at 8..11 = 0xCAFEBABE
    buf[8]  = 0xCA; buf[9]  = 0xFE; buf[10] = 0xBA; buf[11] = 0xBE;
    // Timestamp at 12..15 (zero), Last Rcvd Host Frame at 16..19 (zero).

    cigi_session::HostSession session;
    SofSpy spy;
    session.SetSofProcessor(&spy);
    session.HandleDatagram(buf, sizeof(buf));

    ASSERT_TRUE(spy.got.has_value());
    EXPECT_EQ(spy.got->ig_mode, cigi_session::IgModeRx::Operate);
    EXPECT_EQ(spy.got->ig_frame_number, 0xCAFEBABEu);
    EXPECT_EQ(spy.got->database_id, 0);
}

namespace {
// Fill bytes for a BE double starting at buf[off].
void write_be_double(std::uint8_t * buf, double v) {
    std::uint64_t u;
    std::memcpy(&u, &v, 8);
    for (int i = 0; i < 8; ++i) buf[i] = (u >> (56 - 8 * i)) & 0xFFu;
}

struct RespSpy : cigi_session::IHatHotRespProcessor {
    std::optional<cigi_session::HatHotRespFields> got;
    void OnHatHotResp(const cigi_session::HatHotRespFields & f) override { got = f; }
};
}  // namespace

// Host receive of SOF + HAT/HOT Extended Response in one datagram. The SOF
// carries the Byte Swap Magic so CCL's IncomingMsg can detect orientation;
// the response parser fields (id, hat, hot, material) must round-trip
// through the IHatHotRespProcessor adapter.
TEST(CigiSession, HatHotRespDispatchesToTarget) {
    std::uint8_t buf[24 + 40] = {};

    // SOF header
    buf[0] = 0x65; buf[1] = 24; buf[2] = 3;
    buf[5] = (2u << 4) | (1u << 2) | 1u;  // Minor=2, TSValid=1, Operate
    buf[6] = 0x80; buf[7] = 0x00;         // Byte Swap Magic BE
    buf[8]  = 0; buf[9]  = 0; buf[10] = 0; buf[11] = 1;  // IG frame = 1

    // HAT/HOT Extended Response starts at offset 24.
    std::uint8_t * r = buf + 24;
    r[0] = 0x67; r[1] = 40;
    r[2] = 0x00; r[3] = 0x63;             // HatHotID = 99 (BE)
    r[4] = 0x01 | (0x02 << 1);            // Valid=1, ReqType=Extended(2)
    write_be_double(r + 8,  123.456);     // HAT
    write_be_double(r + 16, 78.901);      // HOT
    r[24] = 0x00; r[25] = 0x00; r[26] = 0x00; r[27] = 0xAB;  // Material

    cigi_session::HostSession session;
    RespSpy spy;
    session.SetHatHotRespProcessor(&spy);
    session.HandleDatagram(buf, sizeof(buf));

    ASSERT_TRUE(spy.got.has_value());
    EXPECT_EQ(spy.got->request_id, 99);
    EXPECT_TRUE(spy.got->valid);
    EXPECT_DOUBLE_EQ(spy.got->hat_m, 123.456);
    EXPECT_DOUBLE_EQ(spy.got->hot_m, 78.901);
    EXPECT_EQ(spy.got->material_code, 0xABu);
}

TEST(CigiSession, AtmosphereControlRoundTrip) {
    cigi_session::HostSession sess;
    sess.BeginFrame(1, 1, 0.0);
    cigi_session::HostSession::AtmosphereFields f{
        /*humidity_pct=*/75,
        /*temperature_c=*/15.0f,
        /*visibility_m=*/10000.0f,
        /*horiz_wind_ms=*/5.0f,
        /*vert_wind_ms=*/0.0f,
        /*wind_direction_deg=*/270.0f,
        /*barometric_pressure_hpa=*/1013.25f,
    };
    sess.AppendAtmosphereControl(f);
    auto [buf, len] = sess.FinishFrame();
    ASSERT_NE(buf, nullptr);
    ASSERT_GE(len, kIgCtrlSize + 32u);

    const std::uint8_t * a = buf + kIgCtrlSize;

    CigiAtmosCtrlV3 cigi;
    ASSERT_GE(cigi.Unpack(const_cast<std::uint8_t *>(a), kSameEndianSwap, nullptr), 0);
    EXPECT_FALSE(cigi.GetAtmosEn());
    EXPECT_EQ(cigi.GetHumidity(), 75);
    EXPECT_FLOAT_EQ(cigi.GetAirTemp(), 15.0f);
    EXPECT_FLOAT_EQ(cigi.GetVisibility(), 10000.0f);
    EXPECT_FLOAT_EQ(cigi.GetHorizWindSp(), 5.0f);
    EXPECT_FLOAT_EQ(cigi.GetVertWindSp(), 0.0f);
    EXPECT_FLOAT_EQ(cigi.GetWindDir(), 270.0f);
    EXPECT_FLOAT_EQ(cigi.GetBaroPress(), 1013.25f);
}

TEST(CigiSession, WeatherControlRoundTrip) {
    cigi_session::HostSession sess;
    sess.BeginFrame(1, 1, 0.0);
    cigi_session::HostSession::WeatherCtrlFields w{};
    w.region_id               = 0;
    w.layer_id                = 2;   // low cumulus
    w.humidity_pct            = 70;
    w.weather_enable          = true;
    w.scud_enable             = false;
    w.cloud_type              = 1;   // Altocumulus per CCL enum
    w.scope                   = cigi_session::HostSession::WeatherScope::Global;
    w.severity                = 1;
    w.air_temp_c              = 12.0f;
    w.visibility_m            = 8000.0f;
    w.scud_frequency_pct      = 0.0f;
    w.coverage_pct            = 0.6f;
    w.base_elevation_m        = 1500.0f;
    w.thickness_m             = 500.0f;
    w.transition_band_m       = 50.0f;
    w.horiz_wind_ms           = 3.0f;
    w.vert_wind_ms            = 0.0f;
    w.wind_direction_deg      = 220.0f;
    w.barometric_pressure_hpa = 1010.0f;
    w.aerosol_concentration_gm3 = 0.0f;
    sess.AppendWeatherControl(w);
    auto [buf, len] = sess.FinishFrame();
    ASSERT_NE(buf, nullptr);
    ASSERT_GE(len, kIgCtrlSize + 56u);

    CigiWeatherCtrlV3 cigi;
    ASSERT_GE(cigi.Unpack(const_cast<std::uint8_t *>(buf + kIgCtrlSize),
                          kSameEndianSwap, nullptr), 0);
    EXPECT_EQ(cigi.GetLayerID(), 2);
    EXPECT_EQ(cigi.GetHumidity(), 70);
    EXPECT_TRUE(cigi.GetWeatherEn());
    EXPECT_EQ(cigi.GetCloudType(), CigiBaseWeatherCtrl::Altocumulus);
    EXPECT_EQ(cigi.GetScope(), CigiBaseWeatherCtrl::Global);
    EXPECT_EQ(cigi.GetSeverity(), 1);
    EXPECT_FLOAT_EQ(cigi.GetAirTemp(), 12.0f);
    EXPECT_FLOAT_EQ(cigi.GetVisibilityRng(), 8000.0f);
    EXPECT_FLOAT_EQ(cigi.GetCoverage(), 0.6f);
    EXPECT_FLOAT_EQ(cigi.GetBaseElev(), 1500.0f);
    EXPECT_FLOAT_EQ(cigi.GetThickness(), 500.0f);
    EXPECT_FLOAT_EQ(cigi.GetTransition(), 50.0f);
    EXPECT_FLOAT_EQ(cigi.GetHorizWindSp(), 3.0f);
    EXPECT_FLOAT_EQ(cigi.GetWindDir(), -140.0f);  // 220° normalized to CCL's [-180,180]
    EXPECT_FLOAT_EQ(cigi.GetBaroPress(), 1010.0f);
}

TEST(CigiSession, EnvRegionControlRoundTrip) {
    cigi_session::HostSession sess;
    sess.BeginFrame(1, 1, 0.0);
    sess.AppendEnvRegionControl(
        /*region_id=*/5,
        cigi_session::HostSession::RegionState::Active,
        /*merge_weather=*/true,
        /*lat=*/50.0, /*lon=*/4.0,
        /*size_x=*/1000.0f, /*size_y=*/500.0f,
        /*corner_radius=*/50.0f, /*rotation=*/45.0f,
        /*transition=*/20.0f);
    auto [buf, len] = sess.FinishFrame();
    ASSERT_NE(buf, nullptr);
    ASSERT_GE(len, kIgCtrlSize + 48u);

    CigiEnvRgnCtrlV3 cigi;
    ASSERT_GE(cigi.Unpack(const_cast<std::uint8_t *>(buf + kIgCtrlSize),
                          kSameEndianSwap, nullptr), 0);
    EXPECT_EQ(cigi.GetRegionID(), 5);
    EXPECT_EQ(cigi.GetRgnState(), CigiBaseEnvRgnCtrl::Active);
    EXPECT_EQ(cigi.GetWeatherProp(), CigiBaseEnvRgnCtrl::Merge);
    EXPECT_DOUBLE_EQ(cigi.GetLat(), 50.0);
    EXPECT_DOUBLE_EQ(cigi.GetLon(), 4.0);
    EXPECT_FLOAT_EQ(cigi.GetXSize(), 1000.0f);
    EXPECT_FLOAT_EQ(cigi.GetYSize(), 500.0f);
    EXPECT_FLOAT_EQ(cigi.GetCornerRadius(), 50.0f);
    EXPECT_FLOAT_EQ(cigi.GetRotation(), 45.0f);
    EXPECT_FLOAT_EQ(cigi.GetTransition(), 20.0f);
}

// Runway friction uses standard Component Control with Class=8
// (GlobalTerrainSurface), ID=100 (GlobalTerrainComponentId::RunwayFriction),
// State=friction level 0..15. Replaces the user-defined 0xCB packet with
// a spec-conformant CIGI encoding every CCL-based IG can dispatch.
TEST(CigiSession, ComponentControlRunwayFriction) {
    cigi_session::HostSession sess;
    sess.BeginFrame(1, 1, 0.0);
    sess.AppendComponentControl(
        cigi_session::HostSession::ComponentClass::GlobalTerrainSurface,
        /*instance=*/0,
        static_cast<std::uint16_t>(cigi_session::GlobalTerrainComponentId::RunwayFriction),
        /*state=*/7);  // wet
    auto [buf, len] = sess.FinishFrame();
    ASSERT_NE(buf, nullptr);
    ASSERT_GE(len, kIgCtrlSize + 32u);

    CigiCompCtrlV3_3 cigi;
    ASSERT_GE(cigi.Unpack(const_cast<std::uint8_t *>(buf + kIgCtrlSize),
                          kSameEndianSwap, nullptr), 0);
    EXPECT_EQ(cigi.GetCompClassV3(), CigiBaseCompCtrl::GlobalTerrainSurfaceV3);
    EXPECT_EQ(cigi.GetCompID(), 100);
    EXPECT_EQ(cigi.GetCompState(), 7);
}

TEST(CigiSession, IgSessionBeginFrameEmitsSof) {
    cigi_session::IgSession ig;
    ig.BeginFrame(/*ig_mode=*/1, /*db_id=*/0, /*ig_frame=*/99,
                  /*last_host_frame=*/100);
    auto [buf, len] = ig.FinishFrame();
    ASSERT_NE(buf, nullptr);
    ASSERT_GE(len, 24u);

    CigiSOFV3_2 pkt;
    ASSERT_GE(pkt.Unpack(const_cast<std::uint8_t *>(buf), kSameEndianSwap, nullptr), 0);
    EXPECT_EQ(pkt.GetIGMode(), CigiBaseSOF::Operate);
    EXPECT_EQ(pkt.GetFrameCntr(), 99u);
    EXPECT_EQ(pkt.GetLastRcvdHostFrame(), 100u);
}

namespace {
struct IgCtrlSpy : cigi_session::IIgCtrlProcessor {
    std::optional<cigi_session::IgCtrlFields> got;
    void OnIgCtrl(const cigi_session::IgCtrlFields & f) override { got = f; }
};
}

// IG session receiving a Host→IG datagram: a HostSession emits a frame
// whose IG Control header carries known fields; an IgSession parses it and
// hands the result to the spy processor. The loopback proves CCL's byte
// swap detection works end-to-end (host-emitted native bytes, IG reads via
// Byte Swap Magic).
TEST(CigiSession, IgSessionIgCtrlDispatchesToTarget) {
    cigi_session::HostSession host;
    host.BeginFrame(/*frame=*/123, /*mode=*/1, /*ts=*/0.5);
    auto [buf, len] = host.FinishFrame();
    ASSERT_NE(buf, nullptr);

    cigi_session::IgSession ig;
    IgCtrlSpy spy;
    ig.SetIgCtrlProcessor(&spy);
    ig.HandleDatagram(buf, len);

    ASSERT_TRUE(spy.got.has_value());
    EXPECT_EQ(spy.got->ig_mode, 1);
    EXPECT_EQ(spy.got->host_frame_number, 123u);
    EXPECT_EQ(spy.got->timestamp_10us_ticks, 500000u);  // 0.5 s × 1e6 µs
}

TEST(CigiSession, IgSessionHatHotXRespRoundTrip) {
    cigi_session::IgSession ig;
    ig.BeginFrame(1, 0, 1, 0);
    ig.AppendHatHotXResp(/*id=*/77, /*valid=*/true,
                          /*hat=*/40.5, /*hot=*/41.7,
                          /*material=*/0xDEu,
                          /*az=*/10.0f, /*el=*/80.0f);
    auto [buf, len] = ig.FinishFrame();
    ASSERT_NE(buf, nullptr);
    ASSERT_GE(len, 24u + 40u);

    CigiHatHotXRespV3_2 resp;
    ASSERT_GE(resp.Unpack(const_cast<std::uint8_t *>(buf + 24), kSameEndianSwap, nullptr), 0);
    EXPECT_EQ(resp.GetHatHotID(), 77);
    EXPECT_TRUE(resp.GetValid());
    EXPECT_DOUBLE_EQ(resp.GetHat(), 40.5);
    EXPECT_DOUBLE_EQ(resp.GetHot(), 41.7);
    EXPECT_EQ(resp.GetMaterial(), 0xDEu);
    EXPECT_FLOAT_EQ(resp.GetNormAz(), 10.0f);
    EXPECT_FLOAT_EQ(resp.GetNormEl(), 80.0f);
}

TEST(CigiSession, HatHotRequestRoundTrip) {
    cigi_session::HostSession sess;
    sess.BeginFrame(/*frame_cntr=*/1, /*ig_mode=*/1, /*ts=*/0.0);
    sess.AppendHatHotRequest(/*id=*/42,
                             /*lat=*/50.901389,
                             /*lon=*/4.484444);
    auto [buf, len] = sess.FinishFrame();
    ASSERT_NE(buf, nullptr);
    ASSERT_GE(len, kIgCtrlSize + 32u);

    // Skip IG Control, parse HAT/HOT Request from offset 24.
    const std::uint8_t * hat_bytes = buf + kIgCtrlSize;

    CigiHatHotReqV3_2 cigi;
    ASSERT_GE(cigi.Unpack(const_cast<std::uint8_t *>(hat_bytes), kSameEndianSwap, nullptr), 0);

    EXPECT_EQ(cigi.GetHatHotID(), 42);
    EXPECT_EQ(cigi.GetReqType(), CigiBaseHatHotReq::Extended);
    EXPECT_EQ(cigi.GetSrcCoordSys(), CigiBaseHatHotReq::Geodetic);
    EXPECT_DOUBLE_EQ(cigi.GetLat(), 50.901389);
    EXPECT_DOUBLE_EQ(cigi.GetLon(), 4.484444);
}
