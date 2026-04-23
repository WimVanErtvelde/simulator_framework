#include <gtest/gtest.h>

#include "cigi_session/HostSession.h"

#include <CigiIGCtrlV3_3.h>
#include <CigiBaseIGCtrl.h>
#include <CigiEntityCtrlV3_3.h>
#include <CigiBaseEntityCtrl.h>
#include <CigiAnimationTable.h>
#include <CigiHatHotReqV3_2.h>
#include <CigiBaseHatHotReq.h>

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
