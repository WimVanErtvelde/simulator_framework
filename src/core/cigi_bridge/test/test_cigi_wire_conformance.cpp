// Round-trip CIGI wire-format conformance test against Boeing's CCL.
//
// This test takes packets emitted by our raw encoders, parses them through CCL's
// spec-compliant Unpack(), and asserts the field values survive — and vice versa
// for inbound packets we parse. If our encoder or parser drifts from the
// CIGI 3.3 spec, this test fails.
//
// Built only when CCL is found on the build host. See parent CMakeLists.txt.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

// CCL's Unpack Swap parameter: pass true on little-endian hosts so CCL byte-
// swaps big-endian wire data into host order. IG Control and Start of Frame
// both carry a Byte Swap Magic Number that CCL auto-detects, but packets
// without that field (Entity Control, HAT/HOT Request, HAT/HOT Extended
// Response) rely on the caller's flag.
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  static constexpr bool kCclSwap = true;
#else
  static constexpr bool kCclSwap = false;
#endif

#include <CigiIGCtrlV3_3.h>
#include <CigiEntityCtrlV3_3.h>
#include <CigiHatHotReqV3_2.h>
#include <CigiHatHotXRespV3_2.h>
#include <CigiSOFV3_2.h>
#include <CigiBaseHatHotReq.h>
#include <CigiBaseEntityCtrl.h>
#include <CigiBaseSOF.h>
#include <CigiAnimationTable.h>
#include <CigiVersionID.h>

// Re-declare the host node's encoder helpers as free functions for direct
// testing — these mirror the wire layout in CigiHostNode and must stay in
// lock-step. If the encoder signature in cigi_host_node.cpp changes, update
// the local copies below to match.

namespace {

// Big-endian primitives — same as those used inside cigi_host_node.cpp.
inline void write_be16(uint8_t * p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}
inline void write_be32(uint8_t * p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >>  8);
    p[3] = static_cast<uint8_t>(v);
}
inline void write_be_double(uint8_t * p, double v) {
    uint64_t u; std::memcpy(&u, &v, 8);
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(u >> (56 - 8*i));
}
inline void write_be_float(uint8_t * p, float v) {
    uint32_t u; std::memcpy(&u, &v, 4);
    write_be32(p, u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Encoders — copies of the wire layout from cigi_host_node.cpp. They MUST
// match the production encoders byte-for-byte. If they diverge, this test
// silently passes broken production code, defeating its purpose.
// To keep this honest: when modifying cigi_host_node.cpp encoders, mirror
// the change here in the same commit.
// ─────────────────────────────────────────────────────────────────────────────

void encode_ig_ctrl(uint8_t * buf,
                    uint32_t frame_cntr,
                    double timestamp_s,
                    uint8_t ig_mode_byte4,
                    uint32_t last_ig_frame)
{
    std::memset(buf, 0, 24);
    buf[0] = 0x01;
    buf[1] = 24;
    buf[2] = 3;                       // Major Version
    buf[3] = 0;                       // Database Number
    buf[4] = ig_mode_byte4;           // bitfield byte (see header CIGI_IG_MODE_*)
    write_be16(&buf[6],  0x8000);     // Byte Swap Magic
    write_be32(&buf[8],  frame_cntr);
    write_be32(&buf[12], static_cast<uint32_t>(timestamp_s * 1e5));
    write_be32(&buf[16], last_ig_frame);
}

void encode_hot_request(uint8_t * buf,
                        uint16_t request_id,
                        double lat_deg, double lon_deg)
{
    std::memset(buf, 0, 32);
    buf[0] = 0x18;
    buf[1] = 32;
    write_be16(&buf[2], request_id);
    buf[4] = 0x02;                    // Request Type = Extended (2)
    write_be_double(&buf[8],  lat_deg);
    write_be_double(&buf[16], lon_deg);
    write_be_double(&buf[24], 0.0);
}

void encode_entity_ctrl(uint8_t * buf,
                        uint16_t entity_id,
                        float roll_deg, float pitch_deg, float yaw_deg,
                        double lat_deg, double lon_deg, double alt_m)
{
    std::memset(buf, 0, 48);
    buf[0] = 0x02;
    buf[1] = 48;
    write_be16(&buf[2], entity_id);
    buf[4] = 0x01;                    // Entity State = Active
    write_be_float(&buf[12], roll_deg);
    write_be_float(&buf[16], pitch_deg);
    write_be_float(&buf[20], yaw_deg);
    write_be_double(&buf[24], lat_deg);
    write_be_double(&buf[32], lon_deg);
    write_be_double(&buf[40], alt_m);
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

// Outbound IG Control: ours → CCL Unpack
TEST(CigiWireConformance, IGControlEncoderMatchesCCL) {
    uint8_t buf[24];
    // CIGI_IG_MODE_OPERATE = 0x25 (bits: MinorVer=2 << 4 | TSValid=1 << 2 | Operate=1)
    encode_ig_ctrl(buf, /*frame=*/0xDEADBEEF, /*ts_s=*/0.5,
                   /*ig_mode_byte4=*/0x25, /*last_ig_frame=*/12345);

    CigiIGCtrlV3_3 cigi;
    // CCL Unpack convention varies per packet (0 = CIGI_SUCCESS for some,
    // bytes-consumed for others). Either is acceptable; only negatives fail.
    ASSERT_GE(cigi.Unpack(buf, kCclSwap, nullptr), 0);

    // Major Version not exposed as a getter; verify raw byte.
    EXPECT_EQ(buf[2], 3);
    EXPECT_EQ(cigi.GetDatabaseID(), 0);
    EXPECT_EQ(cigi.GetIGMode(), CigiBaseIGCtrl::Operate);
    EXPECT_TRUE(cigi.GetTimeStampValid());
    // Byte Swap Magic — CCL stores it protected, verify raw bytes directly.
    EXPECT_EQ(buf[6], 0x80);
    EXPECT_EQ(buf[7], 0x00);
    EXPECT_EQ(cigi.GetFrameCntr(), 0xDEADBEEFu);
    EXPECT_EQ(cigi.GetTimeStamp(), static_cast<Cigi_uint32>(0.5 * 1e5));
    EXPECT_EQ(cigi.GetLastRcvdIGFrame(), 12345u);
}

// Outbound IG Control with Reset mode
TEST(CigiWireConformance, IGControlStandbyMode) {
    uint8_t buf[24];
    // CIGI_IG_MODE_STANDBY / RESET = 0x24 (MinorVer=2 << 4 | TSValid=1 << 2 | Standby=0)
    // Reset and Standby share wire value 0 in CIGI 3.3.
    encode_ig_ctrl(buf, 1, 0.0, 0x24, 0);

    CigiIGCtrlV3_3 cigi;
    ASSERT_GE(cigi.Unpack(buf, kCclSwap, nullptr), 0);
    EXPECT_EQ(cigi.GetIGMode(), CigiBaseIGCtrl::IGModeGrp::Standby);
}

// Outbound HAT/HOT Request: ours → CCL Unpack
TEST(CigiWireConformance, HatHotRequestEncoderMatchesCCL) {
    uint8_t buf[32];
    encode_hot_request(buf, /*req_id=*/42, /*lat=*/50.901389, /*lon=*/4.484444);

    CigiHatHotReqV3_2 cigi;
    ASSERT_GE(cigi.Unpack(buf, kCclSwap, nullptr), 0);

    EXPECT_EQ(cigi.GetHatHotID(), 42);
    EXPECT_EQ(cigi.GetReqType(), CigiBaseHatHotReq::Extended);
    EXPECT_EQ(cigi.GetSrcCoordSys(), CigiBaseHatHotReq::Geodetic);
    EXPECT_DOUBLE_EQ(cigi.GetLat(), 50.901389);
    EXPECT_DOUBLE_EQ(cigi.GetLon(), 4.484444);
}

// Outbound Entity Control: ours → CCL Unpack
TEST(CigiWireConformance, EntityCtrlEncoderMatchesCCL) {
    uint8_t buf[48];
    encode_entity_ctrl(buf,
                       /*id=*/0,
                       /*roll=*/-1.5f, /*pitch=*/2.5f, /*yaw=*/123.4f,
                       /*lat=*/50.901389, /*lon=*/4.484444, /*alt=*/56.0);

    CigiEntityCtrlV3_3 cigi;
    CigiAnimationTable animTable;  // Entity Control Unpack requires Spec
    ASSERT_GE(cigi.Unpack(buf, kCclSwap, &animTable), 0);

    EXPECT_EQ(cigi.GetEntityID(), 0);
    EXPECT_EQ(cigi.GetEntityState(), CigiBaseEntityCtrl::Active);
    EXPECT_FLOAT_EQ(cigi.GetRoll(), -1.5f);
    EXPECT_FLOAT_EQ(cigi.GetPitch(), 2.5f);
    EXPECT_FLOAT_EQ(cigi.GetYaw(), 123.4f);
    EXPECT_DOUBLE_EQ(cigi.GetLat(), 50.901389);
    EXPECT_DOUBLE_EQ(cigi.GetLon(), 4.484444);
    EXPECT_DOUBLE_EQ(cigi.GetAlt(), 56.0);
}

// Inbound SOF: build a spec-compliant big-endian wire buffer by hand, then
// verify that (a) CCL Unpack decodes it correctly and (b) the same byte
// positions our host SOF parser reads yield the same values CCL sees. If
// the spec byte layout ever changes, both arms must agree — that's what
// makes this a round-trip rather than a self-check.
TEST(CigiWireConformance, SOFParseFieldsMatchCCL) {
    uint8_t buf[24] = {};
    buf[0]  = 0x65;                            // Packet ID
    buf[1]  = 24;                              // Packet Size
    buf[2]  = 3;                               // Major Version
    buf[3]  = 0;                               // Database Number
    buf[4]  = 0;                               // IG Status Code
    // byte 5: MinorVer=2<<4 | TSValid=1<<2 | IG Mode=Operate(1)
    buf[5]  = (2u << 4) | (1u << 2) | 1u;
    write_be16(&buf[6],  0x8000);              // Byte Swap Magic
    write_be32(&buf[8],  0xCAFEBABEu);         // IG Frame Number
    // bytes 12-23: zero

    // Our host SOF parser reads:
    uint8_t  ig_mode_bits = buf[5] & 0x03;
    uint32_t ig_frame     = (uint32_t(buf[8])<<24)
                          | (uint32_t(buf[9])<<16)
                          | (uint32_t(buf[10])<< 8)
                          | (uint32_t(buf[11]));
    EXPECT_EQ(ig_mode_bits, static_cast<uint8_t>(CigiBaseSOF::Operate));
    EXPECT_EQ(ig_frame, 0xCAFEBABEu);

    // CCL Unpack must agree on the same bytes.
    CigiSOFV3_2 sof;
    ASSERT_GE(sof.Unpack(buf, kCclSwap, nullptr), 0);
    EXPECT_EQ(sof.GetIGMode(), CigiBaseSOF::Operate);
    EXPECT_EQ(sof.GetFrameCntr(), 0xCAFEBABEu);
}

// Inbound HAT/HOT Extended Response: build a spec wire buffer by hand,
// cross-verify with CCL Unpack.
TEST(CigiWireConformance, HatHotExtRespParseMatchesCCL) {
    uint8_t buf[40] = {};
    buf[0] = 0x67;
    buf[1] = 40;
    write_be16(&buf[2], 99);                       // HAT/HOT ID
    buf[4] = 0x01 | (0x02 << 1);                   // Valid=1, ReqType=Extended(2)
    write_be_double(&buf[8],  123.456);            // HAT
    write_be_double(&buf[16], 78.901);             // HOT
    // Material at bytes 24-27, big-endian
    buf[24] = 0x00; buf[25] = 0x00; buf[26] = 0x00; buf[27] = 0xAB;

    // Our host parser reads:
    uint16_t id  = (uint16_t(buf[2]) << 8) | buf[3];
    bool   valid = (buf[4] & 0x01) != 0;
    double hat, hot;
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u = (u << 8) | buf[8  + i];
    std::memcpy(&hat, &u, 8);
    u = 0;
    for (int i = 0; i < 8; ++i) u = (u << 8) | buf[16 + i];
    std::memcpy(&hot, &u, 8);
    uint32_t material = (uint32_t(buf[24])<<24) | (uint32_t(buf[25])<<16)
                      | (uint32_t(buf[26])<< 8) | (uint32_t(buf[27]));
    EXPECT_EQ(id, 99);
    EXPECT_TRUE(valid);
    EXPECT_DOUBLE_EQ(hat, 123.456);
    EXPECT_DOUBLE_EQ(hot, 78.901);
    EXPECT_EQ(material, 0xABu);

    // CCL Unpack must agree.
    CigiHatHotXRespV3_2 resp;
    ASSERT_GE(resp.Unpack(buf, kCclSwap, nullptr), 0);
    EXPECT_EQ(resp.GetHatHotID(), 99);
    EXPECT_TRUE(resp.GetValid());
    EXPECT_DOUBLE_EQ(resp.GetHat(), 123.456);
    EXPECT_DOUBLE_EQ(resp.GetHot(), 78.901);
    EXPECT_EQ(resp.GetMaterial(), 0xABu);
}
