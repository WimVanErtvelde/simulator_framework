// Unit tests for regional weather encoder functions (Slice 2a).
// Verifies byte layout of Environmental Region Control (0x0B) and
// Weather Control (0x0C) Scope=Regional packets against CIGI 3.3 ICD.

#include <gtest/gtest.h>
#include <cstring>
#include <cmath>

#include "cigi_bridge/weather_encoder.hpp"
#include <sim_msgs/msg/weather_patch.hpp>
#include <sim_msgs/msg/weather_cloud_layer.hpp>
#include <sim_msgs/msg/weather_wind_layer.hpp>

using cigi_bridge::encode_region_control;
using cigi_bridge::encode_regional_cloud_layer;
using cigi_bridge::encode_regional_wind_layer;
using cigi_bridge::encode_regional_scalar_override;

// ─── BE read helpers for test assertions ─────────────────────────────────────
static uint16_t read_be16(const uint8_t * p) {
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

static float read_be_float(const uint8_t * p) {
    uint32_t u = (static_cast<uint32_t>(p[0]) << 24) |
                 (static_cast<uint32_t>(p[1]) << 16) |
                 (static_cast<uint32_t>(p[2]) <<  8) |
                  static_cast<uint32_t>(p[3]);
    float f; std::memcpy(&f, &u, sizeof f); return f;
}

static double read_be_float64(const uint8_t * p) {
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u = (u << 8) | p[i];
    }
    double d; std::memcpy(&d, &u, sizeof d); return d;
}

// ═════════════════════════════════════════════════════════════════════════════
// Environmental Region Control (0x0B)
// ═════════════════════════════════════════════════════════════════════════════

TEST(RegionControl, PacketHeaderAndSize)
{
    sim_msgs::msg::WeatherPatch p;
    p.patch_id  = 42;
    p.lat_deg   = 50.9;
    p.lon_deg   = 4.5;
    p.radius_m  = 9260.0f;  // 5 nm

    uint8_t buf[64] = {};
    size_t n = encode_region_control(buf, sizeof buf, p, /*state=*/1);

    EXPECT_EQ(n, 48u);
    EXPECT_EQ(buf[0], 11);   // Packet ID
    EXPECT_EQ(buf[1], 48);   // Packet Size
    EXPECT_EQ(read_be16(&buf[2]), 42);
}

TEST(RegionControl, RegionStateFlagBits)
{
    sim_msgs::msg::WeatherPatch p;
    p.patch_id = 1;
    p.radius_m = 5000.0f;

    uint8_t buf[48];

    // Active
    encode_region_control(buf, sizeof buf, p, 1);
    EXPECT_EQ(buf[4] & 0x03, 1);
    // Destroyed
    encode_region_control(buf, sizeof buf, p, 2);
    EXPECT_EQ(buf[4] & 0x03, 2);
    // Inactive
    encode_region_control(buf, sizeof buf, p, 0);
    EXPECT_EQ(buf[4] & 0x03, 0);

    // Merge Weather Properties bit (bit 2) always set
    EXPECT_EQ((buf[4] >> 2) & 0x01, 1);
}

TEST(RegionControl, CircularGeometry)
{
    sim_msgs::msg::WeatherPatch p;
    p.patch_id  = 7;
    p.lat_deg   = 50.901234;
    p.lon_deg   = 4.484567;
    p.radius_m  = 5556.0f;   // 3 nm

    uint8_t buf[48];
    encode_region_control(buf, sizeof buf, p, 1);

    EXPECT_DOUBLE_EQ(read_be_float64(&buf[8]),  50.901234);
    EXPECT_DOUBLE_EQ(read_be_float64(&buf[16]),  4.484567);
    EXPECT_FLOAT_EQ(read_be_float(&buf[24]), 0.0f);        // SizeX
    EXPECT_FLOAT_EQ(read_be_float(&buf[28]), 0.0f);        // SizeY
    EXPECT_FLOAT_EQ(read_be_float(&buf[32]), 5556.0f);     // CornerRadius
    EXPECT_FLOAT_EQ(read_be_float(&buf[36]), 0.0f);        // Rotation
    EXPECT_FLOAT_EQ(read_be_float(&buf[40]), 555.6f);      // Transition Perimeter (10%)
}

TEST(RegionControl, InsufficientCapacityReturnsZero)
{
    sim_msgs::msg::WeatherPatch p;
    p.radius_m = 5000.0f;
    uint8_t buf[32];
    EXPECT_EQ(encode_region_control(buf, sizeof buf, p, 1), 0u);
}

// ═════════════════════════════════════════════════════════════════════════════
// Weather Control (0x0C) Scope=Regional — cloud layer
// ═════════════════════════════════════════════════════════════════════════════

TEST(RegionalCloudLayer, PacketHeaderAndScope)
{
    sim_msgs::msg::WeatherCloudLayer l;
    l.cloud_type   = 6;      // Cumulonimbus
    l.coverage_pct = 50.0f;
    l.base_elevation_m = 1000.0f;
    l.thickness_m = 5000.0f;

    uint8_t buf[64] = {};
    size_t n = encode_regional_cloud_layer(buf, sizeof buf, /*region_id=*/42,
                                           /*layer_id=*/1, l, /*enable=*/true);

    EXPECT_EQ(n, 56u);
    EXPECT_EQ(buf[0], 12);
    EXPECT_EQ(buf[1], 56);
    EXPECT_EQ(read_be16(&buf[2]), 42);         // Region ID
    EXPECT_EQ(buf[4], 1);                      // Layer ID
    EXPECT_EQ(buf[7] & 0x03, 1);               // Scope = Regional
}

TEST(RegionalCloudLayer, EnableAndCloudTypeInFlags)
{
    sim_msgs::msg::WeatherCloudLayer l;
    l.cloud_type = 6;  // Cumulonimbus
    uint8_t buf[56];

    encode_regional_cloud_layer(buf, sizeof buf, 1, 1, l, /*enable=*/true);
    EXPECT_EQ(buf[6] & 0x01, 1);               // Weather Enable bit set
    EXPECT_EQ((buf[6] >> 4) & 0x0F, 6);        // Cloud Type nibble = 6

    encode_regional_cloud_layer(buf, sizeof buf, 1, 1, l, /*enable=*/false);
    EXPECT_EQ(buf[6] & 0x01, 0);               // Weather Enable bit cleared
}

TEST(RegionalCloudLayer, LayerFieldsEncoded)
{
    sim_msgs::msg::WeatherCloudLayer l;
    l.cloud_type   = 6;
    l.coverage_pct = 75.0f;
    l.base_elevation_m = 2000.0f;
    l.thickness_m = 8000.0f;
    l.transition_band_m = 300.0f;
    l.scud_enable = true;
    l.scud_frequency_pct = 50.0f;

    uint8_t buf[56];
    encode_regional_cloud_layer(buf, sizeof buf, 1, 1, l, true);

    EXPECT_EQ(buf[6] & 0x02, 0x02);                          // Scud enable bit
    EXPECT_FLOAT_EQ(read_be_float(&buf[16]), 50.0f);         // Scud freq
    EXPECT_FLOAT_EQ(read_be_float(&buf[20]), 75.0f);         // Coverage
    EXPECT_FLOAT_EQ(read_be_float(&buf[24]), 2000.0f);       // Base elevation
    EXPECT_FLOAT_EQ(read_be_float(&buf[28]), 8000.0f);       // Thickness
    EXPECT_FLOAT_EQ(read_be_float(&buf[32]), 300.0f);        // Transition band
}

// ═════════════════════════════════════════════════════════════════════════════
// Weather Control (0x0C) Scope=Regional — wind layer
// ═════════════════════════════════════════════════════════════════════════════

TEST(RegionalWindLayer, WindFieldsEncoded)
{
    sim_msgs::msg::WeatherWindLayer l;
    l.altitude_msl_m     = 3000.0f;
    l.wind_speed_ms      = 15.0f;
    l.vertical_wind_ms   =  1.5f;
    l.wind_direction_deg = 270.0f;

    uint8_t buf[56];
    size_t n = encode_regional_wind_layer(buf, sizeof buf,
                                          /*region_id=*/5,
                                          /*layer_id=*/10, l, true);

    EXPECT_EQ(n, 56u);
    EXPECT_EQ(read_be16(&buf[2]), 5);
    EXPECT_EQ(buf[4], 10);
    EXPECT_EQ(buf[7] & 0x03, 1);                              // Scope=Regional
    EXPECT_EQ((buf[6] >> 4) & 0x0F, 0);                       // Cloud type = None
    EXPECT_FLOAT_EQ(read_be_float(&buf[24]), 3000.0f);        // Altitude
    EXPECT_FLOAT_EQ(read_be_float(&buf[36]), 15.0f);          // Horiz wind speed
    EXPECT_FLOAT_EQ(read_be_float(&buf[40]),  1.5f);          // Vert wind speed
    EXPECT_FLOAT_EQ(read_be_float(&buf[44]), 270.0f);         // Wind direction
}

// ═════════════════════════════════════════════════════════════════════════════
// Scalar override — visibility + temperature
// ═════════════════════════════════════════════════════════════════════════════

TEST(RegionalScalarOverride, VisibilityAndTemperatureEncoded)
{
    uint8_t buf[56];
    size_t n = encode_regional_scalar_override(
        buf, sizeof buf,
        /*region_id=*/7, /*layer_id=*/20,
        /*visibility_m=*/1500.0f,
        /*temperature_k=*/283.15f,            // 10 °C
        /*precip_rate=*/std::nanf(""),
        /*precip_type=*/0,
        /*enable=*/true);

    EXPECT_EQ(n, 56u);
    EXPECT_EQ(read_be16(&buf[2]), 7);
    EXPECT_EQ(buf[4], 20);
    EXPECT_EQ(buf[7] & 0x03, 1);                            // Scope=Regional
    EXPECT_NEAR(read_be_float(&buf[8]), 10.0f, 0.01f);      // °C
    EXPECT_FLOAT_EQ(read_be_float(&buf[12]), 1500.0f);      // Visibility
}

TEST(RegionalScalarOverride, NaNFieldsLeaveBytesUntouched)
{
    uint8_t buf[56];
    std::memset(buf, 0, sizeof buf);

    encode_regional_scalar_override(
        buf, sizeof buf, 1, 20,
        /*vis=*/std::nanf(""),
        /*temp=*/std::nanf(""),
        /*precip_rate=*/std::nanf(""),
        /*precip_type=*/0,
        /*enable=*/true);

    // Temperature at bytes 8-11 and visibility at 12-15 should remain zero
    // because NaN inputs skip the write.
    for (int i = 8; i < 16; ++i) {
        EXPECT_EQ(buf[i], 0u) << "byte " << i << " should be zero with NaN inputs";
    }
}

TEST(RegionalScalarOverride, InsufficientCapacityReturnsZero)
{
    uint8_t buf[40];
    EXPECT_EQ(encode_regional_scalar_override(buf, sizeof buf, 1, 20,
                                              1000.0f, 288.15f,
                                              std::nanf(""), 0, true), 0u);
}

int main(int argc, char ** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
