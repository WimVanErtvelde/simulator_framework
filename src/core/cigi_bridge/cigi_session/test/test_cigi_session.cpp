#include <gtest/gtest.h>

#include "cigi_session/HostSession.h"

#include <CigiIGCtrlV3_3.h>
#include <CigiBaseIGCtrl.h>
#include <CigiEntityCtrlV3_3.h>
#include <CigiBaseEntityCtrl.h>
#include <CigiAnimationTable.h>
#include <CigiHatHotReqV3_2.h>
#include <CigiBaseHatHotReq.h>

#include "cigi_session/processors/ISofProcessor.h"
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
