// Unit tests for WeatherSync diff logic (Slice 2b).
// Verifies that process_update emits Region(Active) for current patches and
// Region(Destroyed) for patches removed from the incoming WeatherState.
// Verifies reposition flush and sanity limit enforcement.

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

#include "cigi_bridge/weather_sync.hpp"
#include "cigi_bridge/weather_encoder.hpp"
#include <sim_msgs/msg/weather_state.hpp>
#include <sim_msgs/msg/weather_patch.hpp>
#include <sim_msgs/msg/weather_cloud_layer.hpp>
#include <sim_msgs/msg/weather_wind_layer.hpp>

using cigi_bridge::WeatherSync;

// ─── BE read helpers (match the patterns used in test_weather_encoder.cpp) ──
static uint16_t read_be16(const uint8_t * p) {
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

// Scan a byte buffer for CIGI packets; return count of packets matching
// (packet_id, optional region_id filter). region_id=UINT16_MAX means "any".
static size_t count_packets(
    const uint8_t * buf, size_t len,
    uint8_t want_packet_id,
    uint16_t want_region_id = 0xFFFF)
{
    size_t count = 0;
    size_t offset = 0;
    while (offset + 2 <= len) {
        uint8_t pkt_id   = buf[offset];
        uint8_t pkt_size = buf[offset + 1];
        if (pkt_size == 0 || offset + pkt_size > len) break;
        if (pkt_id == want_packet_id) {
            if (want_region_id == 0xFFFF ||
                read_be16(&buf[offset + 2]) == want_region_id) {
                ++count;
            }
        }
        offset += pkt_size;
    }
    return count;
}

// Find the first packet with (packet_id, region_id) and return its offset.
// Returns SIZE_MAX if not found.
static size_t find_packet(
    const uint8_t * buf, size_t len,
    uint8_t want_packet_id,
    uint16_t want_region_id)
{
    size_t offset = 0;
    while (offset + 2 <= len) {
        uint8_t pkt_id   = buf[offset];
        uint8_t pkt_size = buf[offset + 1];
        if (pkt_size == 0 || offset + pkt_size > len) break;
        if (pkt_id == want_packet_id && read_be16(&buf[offset + 2]) == want_region_id) {
            return offset;
        }
        offset += pkt_size;
    }
    return SIZE_MAX;
}

// ─── Test fixtures ──────────────────────────────────────────────────────────
static sim_msgs::msg::WeatherPatch make_patch(
    uint16_t id, double lat = 50.9, double lon = 4.5, float radius_m = 5556.0f)
{
    sim_msgs::msg::WeatherPatch p;
    p.patch_id = id;
    p.patch_type = "custom";
    p.label = "test";
    p.lat_deg = lat;
    p.lon_deg = lon;
    p.radius_m = radius_m;
    return p;
}

// ═════════════════════════════════════════════════════════════════════════════
// Addition
// ═════════════════════════════════════════════════════════════════════════════

TEST(WeatherSync, SinglePatchAdditionEmitsRegionActive)
{
    WeatherSync sync;
    sim_msgs::msg::WeatherState ws;
    ws.patches.push_back(make_patch(42));

    uint8_t buf[2048] = {};
    size_t n = sync.process_update(ws, buf, sizeof buf);

    EXPECT_GT(n, 0u);
    EXPECT_EQ(sync.sent_count(), 1u);
    // Region Control packet (ID=11) for region_id=42, state=Active
    size_t off = find_packet(buf, n, /*pkt_id=*/11, /*region_id=*/42);
    ASSERT_NE(off, SIZE_MAX);
    EXPECT_EQ(buf[off + 4] & 0x03, 1);  // Active
}

TEST(WeatherSync, PatchWithLayersEmitsRegionPlusWeatherControl)
{
    WeatherSync sync;
    sim_msgs::msg::WeatherState ws;
    auto p = make_patch(7);
    sim_msgs::msg::WeatherCloudLayer cl;
    cl.cloud_type = 6;
    cl.coverage_pct = 50.0f;
    cl.base_elevation_m = 2000.0f;
    cl.thickness_m = 5000.0f;
    p.cloud_layers.push_back(cl);
    sim_msgs::msg::WeatherWindLayer wl;
    wl.altitude_msl_m = 3000.0f;
    wl.wind_speed_ms = 15.0f;
    wl.wind_direction_deg = 270.0f;
    p.wind_layers.push_back(wl);
    ws.patches.push_back(p);

    uint8_t buf[2048] = {};
    size_t n = sync.process_update(ws, buf, sizeof buf);

    EXPECT_GT(n, 0u);
    // One Region Control + two Weather Control packets for region_id=7
    EXPECT_EQ(count_packets(buf, n, /*Region*/ 11, 7), 1u);
    EXPECT_EQ(count_packets(buf, n, /*Weather*/ 12, 7), 2u);
}

TEST(WeatherSync, MultiplePatchesAllEmitted)
{
    WeatherSync sync;
    sim_msgs::msg::WeatherState ws;
    ws.patches.push_back(make_patch(1));
    ws.patches.push_back(make_patch(2));
    ws.patches.push_back(make_patch(3));

    uint8_t buf[2048] = {};
    size_t n = sync.process_update(ws, buf, sizeof buf);

    EXPECT_EQ(sync.sent_count(), 3u);
    EXPECT_EQ(count_packets(buf, n, 11 /*any Region*/), 3u);
}

// ═════════════════════════════════════════════════════════════════════════════
// Removal
// ═════════════════════════════════════════════════════════════════════════════

TEST(WeatherSync, RemovedPatchEmitsRegionDestroyed)
{
    WeatherSync sync;

    // Frame 1: add patch 42
    sim_msgs::msg::WeatherState ws1;
    ws1.patches.push_back(make_patch(42));
    uint8_t buf[2048] = {};
    sync.process_update(ws1, buf, sizeof buf);
    ASSERT_EQ(sync.sent_count(), 1u);

    // Frame 2: empty patches list
    sim_msgs::msg::WeatherState ws2;
    std::memset(buf, 0, sizeof buf);
    size_t n = sync.process_update(ws2, buf, sizeof buf);

    EXPECT_GT(n, 0u);
    EXPECT_EQ(sync.sent_count(), 0u);
    // Region Control (ID=11) for region_id=42, state=Destroyed
    size_t off = find_packet(buf, n, 11, 42);
    ASSERT_NE(off, SIZE_MAX);
    EXPECT_EQ(buf[off + 4] & 0x03, 2);  // Destroyed
    // No Weather Control packets should be emitted on destroy
    EXPECT_EQ(count_packets(buf, n, 12), 0u);
}

TEST(WeatherSync, PartialRemovalOnlyDestroysMissing)
{
    WeatherSync sync;

    sim_msgs::msg::WeatherState ws1;
    ws1.patches.push_back(make_patch(1));
    ws1.patches.push_back(make_patch(2));
    ws1.patches.push_back(make_patch(3));
    uint8_t buf[2048] = {};
    sync.process_update(ws1, buf, sizeof buf);

    // Frame 2: keep 1 and 3, drop 2
    sim_msgs::msg::WeatherState ws2;
    ws2.patches.push_back(make_patch(1));
    ws2.patches.push_back(make_patch(3));
    std::memset(buf, 0, sizeof buf);
    size_t n = sync.process_update(ws2, buf, sizeof buf);

    EXPECT_EQ(sync.sent_count(), 2u);
    // Destroyed for region_id=2
    size_t off2 = find_packet(buf, n, 11, 2);
    ASSERT_NE(off2, SIZE_MAX);
    EXPECT_EQ(buf[off2 + 4] & 0x03, 2);
    // Active re-emit for region_ids 1 and 3
    size_t off1 = find_packet(buf, n, 11, 1);
    size_t off3 = find_packet(buf, n, 11, 3);
    ASSERT_NE(off1, SIZE_MAX);
    ASSERT_NE(off3, SIZE_MAX);
    EXPECT_EQ(buf[off1 + 4] & 0x03, 1);
    EXPECT_EQ(buf[off3 + 4] & 0x03, 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// Reposition flush
// ═════════════════════════════════════════════════════════════════════════════

TEST(WeatherSync, FlushOnRepositionClearsSentIds)
{
    WeatherSync sync;

    sim_msgs::msg::WeatherState ws;
    ws.patches.push_back(make_patch(5));
    uint8_t buf[2048] = {};
    sync.process_update(ws, buf, sizeof buf);
    ASSERT_EQ(sync.sent_count(), 1u);

    sync.flush_on_reposition();
    EXPECT_EQ(sync.sent_count(), 0u);
}

TEST(WeatherSync, AfterFlushRepublishReEmitsAsFreshAddition)
{
    WeatherSync sync;

    sim_msgs::msg::WeatherState ws;
    ws.patches.push_back(make_patch(5));
    uint8_t buf[2048] = {};
    sync.process_update(ws, buf, sizeof buf);

    sync.flush_on_reposition();
    std::memset(buf, 0, sizeof buf);
    size_t n = sync.process_update(ws, buf, sizeof buf);

    // After flush + same WeatherState, region 5 should emit as Active again
    // and NOT emit Destroyed (sent set was empty at diff time)
    size_t off = find_packet(buf, n, 11, 5);
    ASSERT_NE(off, SIZE_MAX);
    EXPECT_EQ(buf[off + 4] & 0x03, 1);  // Active
    EXPECT_EQ(count_packets(buf, n, 11), 1u);  // exactly one Region Control
}

// ═════════════════════════════════════════════════════════════════════════════
// Sanity limits
// ═════════════════════════════════════════════════════════════════════════════

TEST(WeatherSync, MaxPatchesEnforced)
{
    std::vector<std::string> warnings;
    WeatherSync sync([&](const std::string & m){ warnings.push_back(m); });

    sim_msgs::msg::WeatherState ws;
    for (uint16_t i = 1; i <= WeatherSync::MAX_PATCHES + 5; ++i) {
        ws.patches.push_back(make_patch(i));
    }

    uint8_t buf[8192] = {};
    sync.process_update(ws, buf, sizeof buf);

    EXPECT_EQ(sync.sent_count(), WeatherSync::MAX_PATCHES);
    EXPECT_FALSE(warnings.empty()) << "expected a truncation warning";
}

TEST(WeatherSync, MaxCloudLayersPerPatchEnforced)
{
    WeatherSync sync;
    sim_msgs::msg::WeatherState ws;
    auto p = make_patch(1);
    sim_msgs::msg::WeatherCloudLayer cl;
    cl.cloud_type = 7;
    for (size_t i = 0; i < WeatherSync::MAX_CLOUD_LAYERS + 3; ++i) {
        p.cloud_layers.push_back(cl);
    }
    ws.patches.push_back(p);

    uint8_t buf[4096] = {};
    size_t n = sync.process_update(ws, buf, sizeof buf);

    // Expect exactly MAX_CLOUD_LAYERS cloud Weather Control packets
    EXPECT_EQ(count_packets(buf, n, 12 /*Weather*/, 1), WeatherSync::MAX_CLOUD_LAYERS);
}

// ═════════════════════════════════════════════════════════════════════════════
// Scalar overrides
// ═════════════════════════════════════════════════════════════════════════════

TEST(WeatherSync, VisibilityOverrideEmitted)
{
    WeatherSync sync;
    sim_msgs::msg::WeatherState ws;
    auto p = make_patch(9);
    p.override_visibility = true;
    p.visibility_m = 1500.0f;
    ws.patches.push_back(p);

    uint8_t buf[2048] = {};
    size_t n = sync.process_update(ws, buf, sizeof buf);

    // One Region + one Weather Control (vis+temp at layer_id 20)
    EXPECT_EQ(count_packets(buf, n, 11 /*Region*/, 9), 1u);
    EXPECT_EQ(count_packets(buf, n, 12 /*Weather*/, 9), 1u);
}

TEST(WeatherSync, PrecipitationOverrideEmitsSeparatePacket)
{
    WeatherSync sync;
    sim_msgs::msg::WeatherState ws;
    auto p = make_patch(11);
    p.override_precipitation = true;
    p.precipitation_rate = 0.5f;
    p.precipitation_type = 1;  // Rain
    ws.patches.push_back(p);

    uint8_t buf[2048] = {};
    size_t n = sync.process_update(ws, buf, sizeof buf);

    EXPECT_EQ(count_packets(buf, n, 11 /*Region*/, 11), 1u);
    EXPECT_EQ(count_packets(buf, n, 12 /*Weather*/, 11), 1u);  // precip layer only
}

TEST(WeatherSync, AllOverridesEmitTwoWeatherControls)
{
    WeatherSync sync;
    sim_msgs::msg::WeatherState ws;
    auto p = make_patch(13);
    p.override_visibility = true;
    p.visibility_m = 2000.0f;
    p.override_temperature = true;
    p.temperature_k = 288.15f;
    p.override_precipitation = true;
    p.precipitation_rate = 0.3f;
    p.precipitation_type = 1;
    ws.patches.push_back(p);

    uint8_t buf[2048] = {};
    size_t n = sync.process_update(ws, buf, sizeof buf);

    // One Region, two Weather Control packets (vis+temp combined at id 20,
    // precip separate at id 21).
    EXPECT_EQ(count_packets(buf, n, 11, 13), 1u);
    EXPECT_EQ(count_packets(buf, n, 12, 13), 2u);
}

// ═════════════════════════════════════════════════════════════════════════════
// Slice 2c — Periodic re-assertion
// ═════════════════════════════════════════════════════════════════════════════

TEST(WeatherSyncReassertion, ReEmitsActiveForTrackedPatches)
{
    WeatherSync sync;

    sim_msgs::msg::WeatherState ws;
    ws.patches.push_back(make_patch(1));
    ws.patches.push_back(make_patch(2));
    uint8_t buf[2048] = {};
    sync.process_update(ws, buf, sizeof buf);
    ASSERT_EQ(sync.sent_count(), 2u);

    std::memset(buf, 0, sizeof buf);
    size_t n = sync.emit_reassertion(buf, sizeof buf);

    EXPECT_GT(n, 0u);
    EXPECT_EQ(count_packets(buf, n, 11 /*Region*/), 2u);
    // Both should be Active, not Destroyed
    size_t off1 = find_packet(buf, n, 11, 1);
    size_t off2 = find_packet(buf, n, 11, 2);
    ASSERT_NE(off1, SIZE_MAX);
    ASSERT_NE(off2, SIZE_MAX);
    EXPECT_EQ(buf[off1 + 4] & 0x03, 1);
    EXPECT_EQ(buf[off2 + 4] & 0x03, 1);
}

TEST(WeatherSyncReassertion, NoOutputWhenNoPatchesTracked)
{
    WeatherSync sync;
    uint8_t buf[1024] = {};
    size_t n = sync.emit_reassertion(buf, sizeof buf);
    EXPECT_EQ(n, 0u);
}

TEST(WeatherSyncReassertion, DoesNotModifyTrackedState)
{
    WeatherSync sync;
    sim_msgs::msg::WeatherState ws;
    ws.patches.push_back(make_patch(1));
    uint8_t buf[1024] = {};
    sync.process_update(ws, buf, sizeof buf);
    ASSERT_EQ(sync.sent_count(), 1u);

    sync.emit_reassertion(buf, sizeof buf);
    EXPECT_EQ(sync.sent_count(), 1u);  // unchanged
}

// ═════════════════════════════════════════════════════════════════════════════
// Slice 2c — Destroy retry
// ═════════════════════════════════════════════════════════════════════════════

TEST(WeatherSyncDestroyRetry, InitialDestroyQueuesRemainingRetries)
{
    WeatherSync sync;

    sim_msgs::msg::WeatherState ws;
    ws.patches.push_back(make_patch(99));
    uint8_t buf[1024] = {};
    sync.process_update(ws, buf, sizeof buf);

    // Remove the patch
    sim_msgs::msg::WeatherState ws_empty;
    std::memset(buf, 0, sizeof buf);
    sync.process_update(ws_empty, buf, sizeof buf);

    // After initial destroy: 2 retries pending (DESTROY_RETRY_COUNT=3 total, 1 already emitted)
    EXPECT_TRUE(sync.has_pending_destroys());
    EXPECT_EQ(sync.pending_destroy_count(), WeatherSync::DESTROY_RETRY_COUNT - 1);
}

TEST(WeatherSyncDestroyRetry, SubsequentCallsDrainRetryQueue)
{
    WeatherSync sync;

    sim_msgs::msg::WeatherState ws;
    ws.patches.push_back(make_patch(99));
    uint8_t buf[1024] = {};
    sync.process_update(ws, buf, sizeof buf);

    sim_msgs::msg::WeatherState ws_empty;
    sync.process_update(ws_empty, buf, sizeof buf);
    size_t initial_pending = sync.pending_destroy_count();
    ASSERT_GT(initial_pending, 0u);

    // Each subsequent process_update (or emit_reassertion) decrements retry count.
    // After DESTROY_RETRY_COUNT-1 further calls, queue should be empty.
    for (uint32_t i = 0; i < WeatherSync::DESTROY_RETRY_COUNT - 1; ++i) {
        std::memset(buf, 0, sizeof buf);
        sync.process_update(ws_empty, buf, sizeof buf);
    }
    EXPECT_FALSE(sync.has_pending_destroys());
}

TEST(WeatherSyncDestroyRetry, ReassertionAlsoDrainsPendingDestroys)
{
    WeatherSync sync;

    sim_msgs::msg::WeatherState ws;
    ws.patches.push_back(make_patch(7));
    uint8_t buf[1024] = {};
    sync.process_update(ws, buf, sizeof buf);

    sim_msgs::msg::WeatherState ws_empty;
    sync.process_update(ws_empty, buf, sizeof buf);
    ASSERT_TRUE(sync.has_pending_destroys());

    size_t before = sync.pending_destroy_count();
    std::memset(buf, 0, sizeof buf);
    size_t n = sync.emit_reassertion(buf, sizeof buf);
    EXPECT_GT(n, 0u);
    EXPECT_LT(sync.pending_destroy_count(), before);

    // Destroyed packet for region 7 should appear in re-assertion output
    size_t off = find_packet(buf, n, 11, 7);
    ASSERT_NE(off, SIZE_MAX);
    EXPECT_EQ(buf[off + 4] & 0x03, 2);  // Destroyed
}

TEST(WeatherSyncDestroyRetry, FlushOnRepositionClearsRetries)
{
    WeatherSync sync;

    sim_msgs::msg::WeatherState ws;
    ws.patches.push_back(make_patch(1));
    uint8_t buf[1024] = {};
    sync.process_update(ws, buf, sizeof buf);

    sim_msgs::msg::WeatherState ws_empty;
    sync.process_update(ws_empty, buf, sizeof buf);
    ASSERT_TRUE(sync.has_pending_destroys());

    sync.flush_on_reposition();
    EXPECT_FALSE(sync.has_pending_destroys());
    EXPECT_EQ(sync.sent_count(), 0u);
}

int main(int argc, char ** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
