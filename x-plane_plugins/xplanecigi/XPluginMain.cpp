// XPluginMain.cpp — Minimal raw CIGI 3.3 IG plugin for X-Plane
//
// Self-contained: no CCL, no CIGI_IG_Interface library.
// Parses incoming CIGI 3.3 datagrams directly and sets X-Plane position/attitude.
// Sends a CIGI 3.3 Start-of-Frame (SOF) reply after each received datagram.
//
// Packets handled (host → IG direction):
//   0x01  IG Control (24 bytes) — extract host frame counter
//   0x03  Entity Control (48 bytes) — set ownship position + attitude
//
// Build: mingw-w64 cross-compiler from WSL, see CMakeLists.txt.
// Install: X-Plane/Resources/plugins/xplanecigi/win_x64/xplanecigi.xpl

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>
#include <XPLMGraphics.h>
#include <XPLMDisplay.h>
#include <XPLMScenery.h>
#include <XPLMDataAccess.h>
#include <XPLMWeather.h>

#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include <fstream>
#include <set>
#include <map>
#include <vector>
#include <algorithm>

// ── Network config (loaded from xplanecigi.ini, fallback to defaults) ─────────
static char     g_host_ip[64] = "127.0.0.1";
static uint16_t g_ig_port     = 8002;
static uint16_t g_host_port   = 8001;

// regen_weather gating: "always" | "ground_only" | "never"
enum class RegenMode { Always, GroundOnly, Never };
static RegenMode g_regen_mode = RegenMode::GroundOnly;

static const char * regen_mode_name(RegenMode m)
{
    switch (m) {
        case RegenMode::Always:     return "always";
        case RegenMode::GroundOnly: return "ground_only";
        case RegenMode::Never:      return "never";
    }
    return "unknown";
}

// Read config from <plugin_dir>/xplanecigi.ini
// Format (all optional, unknown keys ignored):
//   host_ip=192.168.1.100
//   ig_port=8002
//   host_port=8001
//   regen_weather=ground_only   (always | ground_only | never)
static void load_config()
{
    char plugin_path[512] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, plugin_path, nullptr, nullptr);

    // Strip filename to get directory (keep trailing separator)
    char * sep = std::strrchr(plugin_path, '/');
    if (!sep) sep = std::strrchr(plugin_path, '\\');
    if (sep) *(sep + 1) = '\0';

    std::string config_path = std::string(plugin_path) + "xplanecigi.ini";
    std::ifstream f(config_path);
    if (!f.is_open()) {
        std::string msg = "xplanecigi: config not found at " + config_path
                        + " — using defaults (127.0.0.1:" + std::to_string(g_ig_port) + ")\n";
        XPLMDebugString(msg.c_str());
        return;
    }

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if      (key == "host_ip")   { std::snprintf(g_host_ip, sizeof(g_host_ip), "%s", val.c_str()); }
        else if (key == "ig_port")   { g_ig_port   = (uint16_t)std::stoi(val); }
        else if (key == "host_port") { g_host_port = (uint16_t)std::stoi(val); }
        else if (key == "regen_weather") {
            if      (val == "always")      g_regen_mode = RegenMode::Always;
            else if (val == "ground_only") g_regen_mode = RegenMode::GroundOnly;
            else if (val == "never")       g_regen_mode = RegenMode::Never;
        }
    }

    char msg[256];
    std::snprintf(msg, sizeof(msg),
        "xplanecigi: config loaded from %s (regen_weather=%s)\n",
        config_path.c_str(), regen_mode_name(g_regen_mode));
    XPLMDebugString(msg);
}

// ── Socket state ──────────────────────────────────────────────────────────────
static SOCKET           g_recv_sock  = INVALID_SOCKET;
static SOCKET           g_send_sock  = INVALID_SOCKET;
static struct sockaddr_in g_host_addr {};

// ── X-Plane datarefs ─────────────────────────────────────────────────────────
static XPLMDataRef g_local_x            = nullptr;
static XPLMDataRef g_local_y            = nullptr;
static XPLMDataRef g_local_z            = nullptr;
static XPLMDataRef g_phi                = nullptr;  // roll  (degrees, X-Plane convention)
static XPLMDataRef g_theta              = nullptr;  // pitch (degrees, X-Plane convention)
static XPLMDataRef g_psi                = nullptr;  // yaw   (degrees, X-Plane convention)
static XPLMDataRef g_override_planepath = nullptr;  // int[20] — index 0 = override user aircraft
static XPLMProbeRef g_terrain_probe    = nullptr;  // terrain elevation probe for HOT requests + stability
static XPLMDataRef g_surface_type       = nullptr;  // int, XP surface enum under aircraft CG

// ── Weather datarefs ─────────────────────────────────────────────────────────
static XPLMDataRef dr_sealevel_temp_c       = nullptr;
static XPLMDataRef dr_sealevel_pressure_pas = nullptr;
static XPLMDataRef dr_visibility_sm         = nullptr;
static XPLMDataRef dr_rain_percent          = nullptr;
static XPLMDataRef dr_cloud_base            = nullptr;
static XPLMDataRef dr_cloud_tops            = nullptr;
static XPLMDataRef dr_cloud_coverage        = nullptr;
static XPLMDataRef dr_cloud_type            = nullptr;
static XPLMDataRef dr_wind_alt              = nullptr;
static XPLMDataRef dr_wind_dir              = nullptr;
static XPLMDataRef dr_wind_spd              = nullptr;
static XPLMDataRef dr_wind_turb             = nullptr;
static XPLMDataRef dr_runway_friction       = nullptr;
static XPLMDataRef dr_update_immediately    = nullptr;
static XPLMDataRef dr_onground_any          = nullptr;
static XPLMCommandRef cmd_regen_weather     = nullptr;

// ── Weather state (decoded from CIGI packets, written to XP by flight loop) ──
struct PendingWeather {
    // From 0x0A Atmosphere Control
    float temperature_c = 15.0f;
    float visibility_m  = 9999.0f;
    float wind_speed_ms = 0.0f;
    float vert_wind_ms  = 0.0f;
    float wind_dir_deg  = 0.0f;
    float pressure_hpa  = 1013.25f;
    uint8_t humidity_pct = 0;
    float rain_pct      = 0.0f;
    uint8_t runway_friction = 0;  // 0=Dry, 1-3=Wet, 4-6=Puddly, 7-9=Snowy, 10-12=Icy, 13-15=Snowy/Icy

    struct CloudLayer {
        float type_xp   = 0.0f;
        float coverage  = 0.0f;
        float base_m    = 0.0f;
        float top_m     = 0.0f;
        bool  valid     = false;
    } cloud[3];

    struct WindLayer {
        float alt_m     = 0.0f;
        float speed_ms  = 0.0f;
        float dir_deg   = 0.0f;
        float vert_ms   = 0.0f;
        float turb      = 0.0f;
        bool  valid     = false;
    } wind[13];

    bool dirty = false;
    bool cloud_changed = false;  // gates regen_weather command — only on cloud/precip edits
};

static PendingWeather      pending_wx;
static XPLMFlightLoopID    weather_flight_loop_id = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Slice 3a — Regional weather decoder state.
// Accumulates Environmental Region Control (0x0B) geometry + Weather Control
// Scope=Regional (0x0C) layers into per-region structs. Slice 3b will feed
// these into XPLMSetWeatherAtLocation.
//
// Region ID 0 is reserved (used for Scope=Global); valid regions are 1-65535.
// ─────────────────────────────────────────────────────────────────────────────
struct PendingCloudLayer {
    uint8_t  layer_id;         // CIGI layer ID (1, 2, 3 for cloud layers)
    uint8_t  cloud_type;       // CIGI enum 0-15
    float    coverage_pct;
    float    base_elevation_m;
    float    thickness_m;
    float    transition_band_m;
    bool     scud_enable;
    float    scud_frequency_pct;
    bool     weather_enable;
};

struct PendingWindLayer {
    uint8_t  layer_id;         // CIGI layer ID (10-12 for wind layers, framework convention)
    float    altitude_msl_m;
    float    wind_speed_ms;
    float    vertical_wind_ms;
    float    wind_direction_deg;
    bool     weather_enable;
};

struct PendingScalarOverride {
    uint8_t  layer_id;         // 20 for vis+temp, 21 for precipitation (framework convention)
    float    visibility_m;     // NaN = not overridden
    float    temperature_c;    // NaN = not overridden (stored as °C as encoded on wire)
    float    precipitation_rate;  // NaN = not overridden
    uint8_t  precipitation_type;  // 0 = not overridden
    bool     weather_enable;
};

struct PendingPatch {
    uint16_t region_id;
    uint8_t  region_state;     // 0=Inactive, 1=Active, 2=Destroyed
    double   lat_deg;
    double   lon_deg;
    float    size_x_m;         // 0 for circular regions (framework convention)
    float    size_y_m;         // 0 for circular regions
    float    corner_radius_m;  // Circle radius
    float    rotation_deg;
    float    transition_perimeter_m;

    std::vector<PendingCloudLayer>     cloud_layers;
    std::vector<PendingWindLayer>      wind_layers;
    std::vector<PendingScalarOverride> scalar_overrides;

    // Set to true whenever any field of this region (or any of its layers)
    // was updated during the last inbound-datagram parse. Slice 3b will
    // consume + clear this to decide when to re-call XPLMSetWeatherAtLocation.
    bool dirty;
};

static std::map<uint16_t, PendingPatch> g_pending_patches;

// ─────────────────────────────────────────────────────────────────────────────
// Slice 3b — Tracks what has been applied to X-Plane via XPLMSetWeatherAtLocation.
// Keyed by Region ID. The stored lat/lon is what was passed to Set; used on
// erase and on move (XP cannot move a sample in place — must erase old lat/lon
// and set at new lat/lon).
//
// Invariant: an entry exists in g_xp_applied iff the Region is currently live
// in X-Plane's weather field. Entries are added after Set succeeds, removed
// after Erase succeeds.
// ─────────────────────────────────────────────────────────────────────────────
struct XpAppliedRegion {
    uint16_t region_id;
    double   lat_deg;
    double   lon_deg;
    float    radius_nm;
};

static std::map<uint16_t, XpAppliedRegion> g_xp_applied;

static float remap_cloud_type(uint8_t cigi_type)
{
    switch (cigi_type) {
        case 5: case 3: case 4:             return 0.0f;  // Cirrus
        case 2: case 8: case 10: case 9:    return 1.0f;  // Stratus
        case 1: case 7:                     return 2.0f;  // Cumulus
        case 6:                             return 3.0f;  // Cumulonimbus
        default:                            return 2.0f;  // fallback Cumulus
    }
}

// ── Frame counters ────────────────────────────────────────────────────────────
static uint32_t g_host_frame = 0;
static uint32_t g_ig_frame   = 0;

// ── IG Mode state (driven by host IG Control packet) ─────────────────────
static uint8_t g_ig_mode             = 2;       // from host: 0=Standby, 1=Reset, 2=Operate
static bool    g_probing_terrain     = false;   // true while waiting for terrain stability
static double  g_last_probe_t        = 0.0;     // last probe wall time
static int     g_probe_stable_count  = 0;       // consecutive stable probes
static double  g_prev_lat            = 0.0;     // last entity lat (for probing position)
static double  g_prev_lon            = 0.0;     // last entity lon
static double  g_last_probe_alt      = 0.0;

// Timing constants (seconds, frame-rate independent)
static constexpr double PROBE_INTERVAL_S   = 0.5;  // probe terrain every 0.5s
static constexpr int    PROBE_STABLE_COUNT = 4;     // 4 stable probes = 2s of stability
static constexpr double PROBE_TOLERANCE_M  = 1.0;   // probes within 1m = stable

// ─────────────────────────────────────────────────────────────────────────────
// Big-endian read helpers
// ─────────────────────────────────────────────────────────────────────────────
static uint16_t read_be16(const uint8_t * p)
{
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint32_t read_be32(const uint8_t * p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static float read_be_float(const uint8_t * p)
{
    uint32_t v = read_be32(p);
    float    f;
    std::memcpy(&f, &v, 4);
    return f;
}

static double read_be_double(const uint8_t * p)
{
    uint64_t hi = read_be32(p);
    uint64_t lo = read_be32(p + 4);
    uint64_t v  = (hi << 32) | lo;
    double   d;
    std::memcpy(&d, &v, 8);
    return d;
}

// ─────────────────────────────────────────────────────────────────────────────
// Big-endian write helper
// ─────────────────────────────────────────────────────────────────────────────
static void write_be16(uint8_t * p, uint16_t v)
{
    p[0] = (v >> 8) & 0xFF;
    p[1] =  v       & 0xFF;
}

static void write_be32(uint8_t * p, uint32_t v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >>  8) & 0xFF;
    p[3] =  v        & 0xFF;
}

static void write_be_double(uint8_t * p, double v)
{
    uint64_t u;
    std::memcpy(&u, &v, sizeof u);
    p[0] = (uint8_t)(u >> 56); p[1] = (uint8_t)(u >> 48);
    p[2] = (uint8_t)(u >> 40); p[3] = (uint8_t)(u >> 32);
    p[4] = (uint8_t)(u >> 24); p[5] = (uint8_t)(u >> 16);
    p[6] = (uint8_t)(u >>  8); p[7] = (uint8_t) u;
}

static void write_be_float(uint8_t * p, float v)
{
    uint32_t u;
    std::memcpy(&u, &v, sizeof u);
    write_be32(p, u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Surface type mapping: X-Plane enum → framework enum (see HatHotResponse.msg)
//
// X-Plane surface enum:
//   0=surf_none, 1=surf_water, 2=surf_concrete, 3=surf_asphalt, 4=surf_grass,
//   5=surf_dirt, 6=surf_gravel, 7=surf_lake, 8=surf_snow, 9=surf_shoulder,
//   10=surf_blastpad, 11=surf_grnd, 12=surf_object
//
// Framework enum:
//   0=UNKNOWN, 1=ASPHALT, 2=CONCRETE, 3=GRASS, 4=DIRT, 5=GRAVEL,
//   6=WATER, 7=SNOW_COVERED, 8=ICE_SURFACE, 9=SAND, 10=MARSH
// ─────────────────────────────────────────────────────────────────────────────
static uint8_t xp_surface_to_framework(int xp_type)
{
    switch (xp_type) {
        case 0:  return 0;   // surf_none      → UNKNOWN
        case 1:  return 6;   // surf_water     → WATER
        case 2:  return 2;   // surf_concrete  → CONCRETE
        case 3:  return 1;   // surf_asphalt   → ASPHALT
        case 4:  return 3;   // surf_grass     → GRASS
        case 5:  return 4;   // surf_dirt      → DIRT
        case 6:  return 5;   // surf_gravel    → GRAVEL
        case 7:  return 6;   // surf_lake      → WATER
        case 8:  return 7;   // surf_snow      → SNOW_COVERED
        case 9:  return 1;   // surf_shoulder  → ASPHALT (paved shoulder)
        case 10: return 2;   // surf_blastpad  → CONCRETE
        case 11: return 4;   // surf_grnd      → DIRT (generic ground)
        case 12: return 0;   // surf_object    → UNKNOWN
        default: {
            static std::set<int> s_seen;
            if (s_seen.insert(xp_type).second) {
                char dbg[128];
                std::snprintf(dbg, sizeof(dbg),
                    "xplanecigi: unknown XP surface_type=%d → UNKNOWN(0) "
                    "(add to xp_surface_to_framework mapping)\n", xp_type);
                XPLMDebugString(dbg);
            }
            return 0;  // UNKNOWN
        }
    }
}

// Default friction factors per framework surface enum.
// Values match HatHotResponse.msg convention (JSBSim-style multipliers):
//   static_friction_factor  — braking/static grip   (1.0 = dry pavement baseline)
//   rolling_friction_factor — rolling drag multiplier (>1.0 = softer surface)
static void default_friction_for_surface(uint8_t fw_surface, float & static_ff, float & rolling_ff)
{
    switch (fw_surface) {
        case 1:  static_ff = 1.00f; rolling_ff = 1.00f; return;  // ASPHALT
        case 2:  static_ff = 0.95f; rolling_ff = 0.90f; return;  // CONCRETE
        case 3:  static_ff = 0.50f; rolling_ff = 3.00f; return;  // GRASS
        case 4:  static_ff = 0.40f; rolling_ff = 4.00f; return;  // DIRT
        case 5:  static_ff = 0.45f; rolling_ff = 3.50f; return;  // GRAVEL
        case 6:  static_ff = 0.05f; rolling_ff = 0.10f; return;  // WATER
        case 7:  static_ff = 0.30f; rolling_ff = 2.00f; return;  // SNOW_COVERED
        case 8:  static_ff = 0.10f; rolling_ff = 0.80f; return;  // ICE_SURFACE
        case 9:  static_ff = 0.35f; rolling_ff = 5.00f; return;  // SAND
        case 10: static_ff = 0.30f; rolling_ff = 6.00f; return;  // MARSH
        default: static_ff = 1.00f; rolling_ff = 1.00f; return;  // UNKNOWN → asphalt
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Send CIGI 3.3 Start-of-Frame (SOF) to host
// ─────────────────────────────────────────────────────────────────────────────
static void send_sof()
{
    if (g_send_sock == INVALID_SOCKET) return;

    uint8_t buf[32] = {};
    buf[0] = 0x01;   // Packet ID = 1 (Start of Frame, IG→host direction)
    buf[1] = 0x20;   // Packet Size = 32
    buf[2] = 0x00;   // DB Number = 0 (database load complete)
    buf[3] = 0x00;   // IG Status Code = 0 (OK)
    buf[4] = g_probing_terrain ? 0x00 : 0x02;  // Standby while probing terrain, Normal when ready
    // buf[5..7] = 0 (Timestamp Valid=0, ERM=WGS84)
    // buf[8..11] = 0 (simulation timestamp — not used)
    write_be32(buf + 12, g_host_frame);   // echo host frame counter
    write_be32(buf + 16, g_ig_frame);     // IG frame counter
    // buf[20..31] = 0

    sendto(g_send_sock,
           reinterpret_cast<const char *>(buf), 32, 0,
           reinterpret_cast<const struct sockaddr *>(&g_host_addr),
           sizeof(g_host_addr));
}

// ─────────────────────────────────────────────────────────────────────────────
// Process a single CIGI 3.3 packet
// ─────────────────────────────────────────────────────────────────────────────
static void process_packet(const uint8_t * pkt, int len)
{
    if (len < 2) return;

    const uint8_t packet_id   = pkt[0];
    const uint8_t packet_size = pkt[1];
    if (packet_size < 2 || packet_size > (uint8_t)len) return;

    switch (packet_id)
    {
    // ── IG Control (host → IG) ─────────────────────────────────────────────
    // Byte 3 bits[1:0] = IG Mode: 0=Standby, 1=Reset, 2=Operate
    case 0x01:
        if (packet_size >= 12) {
            g_host_frame = read_be32(pkt + 8);
            uint8_t new_mode = pkt[3] & 0x03;
            if (new_mode != g_ig_mode) {
                char dbg[128];
                static const char * mode_names[] = {"Standby", "Reset", "Operate", "Debug"};
                snprintf(dbg, sizeof(dbg), "xplanecigi: IG Mode %s → %s\n",
                         mode_names[g_ig_mode & 0x03], mode_names[new_mode & 0x03]);
                XPLMDebugString(dbg);

                // Reset → Operate transition: begin terrain probe stability check
                if (g_ig_mode == 1 && new_mode == 2) {
                    g_probing_terrain = true;
                    g_probe_stable_count = 0;
                    g_last_probe_t = 0.0;
                    g_last_probe_alt = 0.0;
                    XPLMDebugString("xplanecigi: terrain probing started (Reset → Operate)\n");
                }
                g_ig_mode = new_mode;
            }
        }
        break;

    // ── Entity Control ─────────────────────────────────────────────────────
    // Layout (CIGI 3.3, big-endian, 48 bytes):
    //   [0]     packet_id = 0x03
    //   [1]     packet_size = 48
    //   [2-3]   entity_id (uint16 BE)
    //   [4]     entity_state + flags
    //   [5-11]  attach state, type id, parent id, alpha
    //   [12-15] roll  (float32 BE, degrees)
    //   [16-19] pitch (float32 BE, degrees)
    //   [20-23] yaw   (float32 BE, degrees)
    //   [24-31] lat   (float64 BE, degrees)
    //   [32-39] lon   (float64 BE, degrees)
    //   [40-47] alt   (float64 BE, metres MSL)
    case 0x03:
        if (packet_size >= 48)
        {
            const float  roll_deg  = read_be_float(pkt + 12);
            const float  pitch_deg = read_be_float(pkt + 16);
            const float  yaw_deg   = read_be_float(pkt + 20);
            const double lat       = read_be_double(pkt + 24);
            const double lon       = read_be_double(pkt + 32);
            const double alt       = read_be_double(pkt + 40);

            // Convert geodetic → X-Plane local OpenGL coordinates
            double lx, ly, lz;
            XPLMWorldToLocal(lat, lon, alt, &lx, &ly, &lz);

            // Diagnostic: log Entity Control position vs X-Plane readback (1/sec)
            {
                static double s_last_log_t = 0.0;
                double now_t = XPLMGetElapsedTime();
                if (now_t - s_last_log_t >= 1.0) {
                    s_last_log_t = now_t;
                    // Read current X-Plane position BEFORE we overwrite
                    double cur_lx = XPLMGetDatad(g_local_x);
                    double cur_ly = XPLMGetDatad(g_local_y);
                    double cur_lz = XPLMGetDatad(g_local_z);
                    double cur_lat, cur_lon, cur_alt;
                    XPLMLocalToWorld(cur_lx, cur_ly, cur_lz, &cur_lat, &cur_lon, &cur_alt);
                    char dbg[512];
                    snprintf(dbg, sizeof(dbg),
                        "xplanecigi: ENTITY lat=%.6f lon=%.6f alt=%.1f | XPLANE lat=%.6f lon=%.6f alt=%.1f | dlat=%.6f\n",
                        lat, lon, alt, cur_lat, cur_lon, cur_alt, lat - cur_lat);
                    XPLMDebugString(dbg);
                }
            }

            XPLMSetDatad(g_local_x, lx);
            XPLMSetDatad(g_local_y, ly);
            XPLMSetDatad(g_local_z, lz);

            // X-Plane sign convention for Euler angles:
            //   phi   (roll)  =  CIGI bank  (positive = right wing down)
            //   theta (pitch) =  CIGI pitch (positive = nose up, same convention)
            //   psi   (yaw)   =  CIGI heading
            XPLMSetDataf(g_phi,    roll_deg);
            XPLMSetDataf(g_theta,  pitch_deg);
            XPLMSetDataf(g_psi,    yaw_deg);

            // Track entity position for terrain probing
            g_prev_lat = lat;
            g_prev_lon = lon;
        }
        break;

    // ── HAT/HOT Request (host → IG) ──────────────────────────────────────
    // CIGI 3.3, Packet ID = 0x18, Size = 32 bytes
    //   [2-3]   Request ID (uint16 BE)
    //   [4]     Request Type bits[1:0] (0=HAT, 1=HOT, 2=Extended)
    //   [12-19] Latitude  (float64 BE, degrees)
    //   [20-27] Longitude (float64 BE, degrees)
    //   [28-31] Altitude  (float32 BE, metres — ignored for HOT)
    case 0x18:
        if (packet_size >= 32 && g_terrain_probe && g_send_sock != INVALID_SOCKET)
        {   // Always respond — host side decides whether to trust based on timing
            const uint16_t req_id  = read_be16(pkt + 2);
            const double   lat_deg = read_be_double(pkt + 12);
            const double   lon_deg = read_be_double(pkt + 20);

            // Convert geodetic to X-Plane local coords
            double lx, ly, lz;
            XPLMWorldToLocal(lat_deg, lon_deg, 0.0, &lx, &ly, &lz);

            // Probe terrain
            XPLMProbeInfo_t info;
            info.structSize = sizeof(info);
            XPLMProbeResult result = XPLMProbeTerrainXYZ(
                g_terrain_probe,
                static_cast<float>(lx), 0.0f, static_cast<float>(lz),
                &info);

            bool   valid  = (result == xplm_ProbeHitTerrain);
            double hot_msl = 0.0;

            if (valid) {
                double out_lat, out_lon, out_alt;
                XPLMLocalToWorld(info.locationX, info.locationY, info.locationZ,
                                 &out_lat, &out_lon, &out_alt);
                hot_msl = out_alt;
            }

            // Look up surface type under aircraft CG (same for all gear points,
            // since XP's terrain probe API doesn't expose surface type per lat/lon)
            uint8_t fw_surface = 0;
            float   static_ff  = 1.0f;
            float   rolling_ff = 1.0f;
            if (g_surface_type) {
                int xp_surface = XPLMGetDatai(g_surface_type);
                fw_surface = xp_surface_to_framework(xp_surface);
                default_friction_for_surface(fw_surface, static_ff, rolling_ff);
            }

            // Send HOT Response (0x02, 48 bytes — standard 16 + HAT placeholder +
            // extended fields: surface_type at 24, friction factors at 28/32)
            uint8_t resp[48] = {};
            resp[0] = 0x02;        // Packet ID = HAT/HOT Response
            resp[1] = 48;          // Packet Size
            write_be16(resp + 2, req_id);
            resp[4] = valid ? 0x01 : 0x00;         // Valid flag
            write_be_double(resp + 8, hot_msl);     // HOT (terrain MSL, metres)
            write_be_double(resp + 16, 0.0);        // HAT placeholder
            resp[24] = fw_surface;                  // framework surface enum
            // resp[25..27] reserved (zeroed)
            write_be_float(resp + 28, static_ff);   // static friction factor
            write_be_float(resp + 32, rolling_ff);  // rolling friction factor
            // resp[36..47] reserved (zeroed)

            sendto(g_send_sock,
                   reinterpret_cast<const char *>(resp), 48, 0,
                   reinterpret_cast<const struct sockaddr *>(&g_host_addr),
                   sizeof(g_host_addr));
        }
        break;

    // ── Atmosphere Control (host → IG) ──────────────────────────────────────
    // Packet ID = 0x0A, Size = 32 bytes
    case 0x0A:
        if (packet_size >= 32) {
            pending_wx.humidity_pct  = pkt[3];
            pending_wx.temperature_c = read_be_float(pkt + 4);
            pending_wx.visibility_m  = read_be_float(pkt + 8);
            pending_wx.wind_speed_ms = read_be_float(pkt + 12);
            pending_wx.vert_wind_ms  = read_be_float(pkt + 16);
            pending_wx.wind_dir_deg  = read_be_float(pkt + 20);
            pending_wx.pressure_hpa  = read_be_float(pkt + 24);
            pending_wx.dirty = true;
        }
        break;

    // ── Environmental Region Control (host → IG) ────────────────────────────
    // Packet ID = 0x0B, Size = 48 bytes. Slice 3a: parse + accumulate into
    // g_pending_patches. No XP SDK call yet — Slice 3b wires the behavior in.
    case 0x0B:
        if (packet_size != 48) {
            char msg[128];
            snprintf(msg, sizeof msg,
                "xplanecigi: unexpected Env Region Ctrl size %u (want 48) — skipping\n",
                packet_size);
            XPLMDebugString(msg);
            break;
        }
        {
            uint16_t region_id    = read_be16(pkt + 2);
            uint8_t  flags        = pkt[4];
            uint8_t  region_state = flags & 0x03;    // bits 0-1
            // bit 2 = Merge Weather Properties (informational, we always merge)

            double lat         = read_be_double(pkt + 8);
            double lon         = read_be_double(pkt + 16);
            float  size_x      = read_be_float(pkt + 24);
            float  size_y      = read_be_float(pkt + 28);
            float  radius      = read_be_float(pkt + 32);
            float  rotation    = read_be_float(pkt + 36);
            float  trans_perim = read_be_float(pkt + 40);

            if (region_id == 0) {
                // Region 0 is reserved (used by Scope=Global). Shouldn't be set via
                // Env Region Ctrl — log and ignore.
                XPLMDebugString("xplanecigi: Env Region Ctrl with region_id=0 — ignoring\n");
                break;
            }

            if (region_state == 2) {
                // Destroyed — remove from pending map (Slice 3b will fire XPLMEraseWeatherAtLocation)
                auto it = g_pending_patches.find(region_id);
                if (it != g_pending_patches.end()) {
                    g_pending_patches.erase(it);
                    char msg[128];
                    snprintf(msg, sizeof msg,
                        "xplanecigi: Region %u destroyed (pending erase)\n", region_id);
                    XPLMDebugString(msg);
                }
            } else {
                // Active (or Inactive — treat Inactive as Active for now; cigi_bridge
                // currently emits only Active or Destroyed).
                PendingPatch & p = g_pending_patches[region_id];
                p.region_id = region_id;
                p.region_state = region_state;
                p.lat_deg = lat;
                p.lon_deg = lon;
                p.size_x_m = size_x;
                p.size_y_m = size_y;
                p.corner_radius_m = radius;
                p.rotation_deg = rotation;
                p.transition_perimeter_m = trans_perim;
                p.dirty = true;

                char msg[192];
                snprintf(msg, sizeof msg,
                    "xplanecigi: Region %u state=%u lat=%.6f lon=%.6f radius=%.1fm transition=%.1fm\n",
                    region_id, region_state, lat, lon, radius, trans_perim);
                XPLMDebugString(msg);
            }
        }
        break;

    // ── Weather Control (host → IG) ──────────────────────────────────────────
    // Packet ID = 0x0C, Size = 56 bytes
    //
    // Byte 7 bits[1:0] = Scope: 0=Global, 1=Regional, 2=Entity.
    // Scope=Regional routes into g_pending_patches (Slice 3a); Scope=Global
    // falls through to the pre-existing pending_wx path (unchanged behavior).
    case 0x0C:
        if (packet_size >= 56) {
            uint8_t layer_id = pkt[4];
            uint8_t flags1   = pkt[6];
            uint8_t flags2   = pkt[7];
            uint8_t scope    = flags2 & 0x03;
            bool weather_enable      = (flags1 & 0x01) != 0;
            bool scud_enable         = (flags1 & 0x02) != 0;
            uint8_t cloud_type_cigi  = (flags1 >> 4) & 0x0F;
            uint8_t severity         = (flags2 >> 2) & 0x07;

            float coverage_pct = read_be_float(pkt + 20);
            float base_elev_m  = read_be_float(pkt + 24);
            float thickness_m  = read_be_float(pkt + 28);
            float h_wind_ms    = read_be_float(pkt + 36);
            float v_wind_ms    = read_be_float(pkt + 40);
            float wind_dir     = read_be_float(pkt + 44);

            if (scope == 1 /* Regional */) {
                // Slice 3a regional decoder — accumulate into g_pending_patches.
                // Region ID is carried in bytes 2-3 when Scope=Regional.
                uint16_t region_id = read_be16(pkt + 2);
                if (region_id == 0) {
                    XPLMDebugString("xplanecigi: Weather Ctrl Scope=Regional with region_id=0 — ignoring\n");
                    break;
                }

                // cigi_bridge is supposed to emit Env Region Ctrl BEFORE any
                // Weather Control referencing that region. If the region is not
                // in g_pending_patches we still auto-create a stub — missing
                // geometry will be filled in on the next datagram.
                PendingPatch & p = g_pending_patches[region_id];
                p.region_id = region_id;
                p.dirty = true;

                if (layer_id >= 1 && layer_id <= 3) {
                    // Cloud layer slot
                    PendingCloudLayer cl;
                    cl.layer_id           = layer_id;
                    cl.cloud_type         = cloud_type_cigi;
                    cl.coverage_pct       = coverage_pct;
                    cl.base_elevation_m   = base_elev_m;
                    cl.thickness_m        = thickness_m;
                    cl.transition_band_m  = read_be_float(pkt + 32);
                    cl.scud_enable        = scud_enable;
                    cl.scud_frequency_pct = read_be_float(pkt + 16);
                    cl.weather_enable     = weather_enable;

                    auto it = std::find_if(p.cloud_layers.begin(), p.cloud_layers.end(),
                        [&](const PendingCloudLayer & x){ return x.layer_id == layer_id; });
                    if (it != p.cloud_layers.end()) *it = cl; else p.cloud_layers.push_back(cl);

                    char msg[192];
                    snprintf(msg, sizeof msg,
                        "xplanecigi: Region %u cloud layer %u type=%u cov=%.1f%% base=%.0fm thick=%.0fm en=%d\n",
                        region_id, layer_id, cloud_type_cigi,
                        cl.coverage_pct, cl.base_elevation_m, cl.thickness_m, weather_enable);
                    XPLMDebugString(msg);
                } else if (layer_id >= 10 && layer_id <= 12) {
                    // Wind layer slot
                    PendingWindLayer wl;
                    wl.layer_id           = layer_id;
                    wl.altitude_msl_m     = base_elev_m;
                    wl.wind_speed_ms      = h_wind_ms;
                    wl.vertical_wind_ms   = v_wind_ms;
                    wl.wind_direction_deg = wind_dir;
                    wl.weather_enable     = weather_enable;

                    auto it = std::find_if(p.wind_layers.begin(), p.wind_layers.end(),
                        [&](const PendingWindLayer & x){ return x.layer_id == layer_id; });
                    if (it != p.wind_layers.end()) *it = wl; else p.wind_layers.push_back(wl);

                    char msg[192];
                    snprintf(msg, sizeof msg,
                        "xplanecigi: Region %u wind layer %u alt=%.0fm spd=%.1fm/s dir=%.0fdeg en=%d\n",
                        region_id, layer_id, wl.altitude_msl_m,
                        wl.wind_speed_ms, wl.wind_direction_deg, weather_enable);
                    XPLMDebugString(msg);
                } else if (layer_id == 20 || layer_id == 21) {
                    // Scalar override: vis+temp (20) or precipitation (21)
                    PendingScalarOverride ov;
                    ov.layer_id           = layer_id;
                    ov.temperature_c      = read_be_float(pkt +  8);
                    ov.visibility_m       = read_be_float(pkt + 12);
                    // precipitation rate/type not currently carried in Weather Control
                    // by the encoder — left NaN/0 for now. Slice 3b may expand once the
                    // encoder's precipitation path is finalized.
                    ov.precipitation_rate = std::nanf("");
                    ov.precipitation_type = 0;
                    ov.weather_enable     = weather_enable;

                    auto it = std::find_if(p.scalar_overrides.begin(), p.scalar_overrides.end(),
                        [&](const PendingScalarOverride & x){ return x.layer_id == layer_id; });
                    if (it != p.scalar_overrides.end()) *it = ov; else p.scalar_overrides.push_back(ov);

                    char msg[192];
                    snprintf(msg, sizeof msg,
                        "xplanecigi: Region %u scalar override layer %u vis=%.0fm temp=%.1fC en=%d\n",
                        region_id, layer_id, ov.visibility_m, ov.temperature_c, weather_enable);
                    XPLMDebugString(msg);
                } else {
                    char msg[128];
                    snprintf(msg, sizeof msg,
                        "xplanecigi: Region %u unknown layer_id %u — ignoring\n",
                        region_id, layer_id);
                    XPLMDebugString(msg);
                }
                break;
            }

            // Scope=Global (0): legacy behavior — write into global pending_wx state
            // via sim/weather/region/* datarefs. Existed before Slice 3a when the plugin
            // didn't distinguish scope; retained unchanged for backward compatibility
            // with the existing global weather path.
            if (scope == 0 /* Global */) {
                if (layer_id >= 1 && layer_id <= 3) {
                    // Cloud layer — respect enable flag so host can disable a removed layer
                    int idx = layer_id - 1;
                    auto & slot = pending_wx.cloud[idx];
                    if (weather_enable) {
                        float new_type = remap_cloud_type(cloud_type_cigi);
                        float new_cov  = coverage_pct / 100.0f;
                        float new_top  = base_elev_m + thickness_m;
                        bool differs = !slot.valid
                            || slot.type_xp  != new_type
                            || slot.coverage != new_cov
                            || slot.base_m   != base_elev_m
                            || slot.top_m    != new_top;
                        slot.type_xp  = new_type;
                        slot.coverage = new_cov;
                        slot.base_m   = base_elev_m;
                        slot.top_m    = new_top;
                        slot.valid    = true;
                        if (differs) pending_wx.cloud_changed = true;
                    } else {
                        if (slot.valid) pending_wx.cloud_changed = true;
                        slot.type_xp  = 0.0f;
                        slot.coverage = 0.0f;
                        slot.base_m   = 0.0f;
                        slot.top_m    = 0.0f;
                        slot.valid    = false;
                    }
                } else if (!weather_enable) {
                    break;  // precipitation/wind layers — legacy behavior
                } else if (layer_id == 4 || layer_id == 5) {
                    // Precipitation
                    float new_rain = coverage_pct / 100.0f;
                    if (new_rain != pending_wx.rain_pct) pending_wx.cloud_changed = true;
                    pending_wx.rain_pct = new_rain;
                } else if (layer_id >= 10) {
                    // Wind-only layer
                    int wind_idx = layer_id - 10;
                    if (wind_idx < 13) {
                        pending_wx.wind[wind_idx].alt_m     = base_elev_m;
                        pending_wx.wind[wind_idx].speed_ms  = h_wind_ms;
                        pending_wx.wind[wind_idx].dir_deg   = wind_dir;
                        pending_wx.wind[wind_idx].vert_ms   = v_wind_ms;
                        pending_wx.wind[wind_idx].turb      = severity / 5.0f;
                        pending_wx.wind[wind_idx].valid     = true;
                    }
                }
                pending_wx.dirty = true;
                break;
            }

            // Scope 2 (Entity) or unknown — not used by this framework
            {
                char msg[96];
                snprintf(msg, sizeof msg,
                    "xplanecigi: Weather Ctrl unexpected scope=%u — ignoring\n", scope);
                XPLMDebugString(msg);
            }
        }
        break;

    // ── Runway Friction (user-defined, host → IG) ───────────────────────────
    // Packet ID = 0xCB, Size = 8 bytes
    case 0xCB:
        if (packet_size >= 8) {
            pending_wx.runway_friction = pkt[2];
            pending_wx.dirty = true;
        }
        break;

    default:
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse a CIGI datagram (may contain multiple back-to-back packets)
// ─────────────────────────────────────────────────────────────────────────────
static void process_datagram(const uint8_t * buf, int len)
{
    int offset = 0;
    while (offset + 2 <= len)
    {
        const uint8_t pkt_size = buf[offset + 1];
        if (pkt_size < 2 || offset + pkt_size > len) break;
        process_packet(buf + offset, pkt_size);
        offset += pkt_size;
    }
    ++g_ig_frame;
    send_sof();
}

// ─────────────────────────────────────────────────────────────────────────────
// Slice 3b conversion helpers
// ─────────────────────────────────────────────────────────────────────────────
static inline float meters_to_nm(float m) {
    return m * 0.000539957f;
}

static inline float meters_to_ft(float m) {
    return m * 3.28084f;
}

// Derive max altitude MSL (feet) from a patch's authored layers, plus 2000 ft
// buffer. Falls back to XPLM_DEFAULT_WXR_LIMIT_MSL_FT (10000 ft) if no layers.
static float derive_max_alt_ft(const PendingPatch & p) {
    float max_m = 0.0f;
    for (const auto & cl : p.cloud_layers) {
        if (!cl.weather_enable) continue;
        float top_m = cl.base_elevation_m + cl.thickness_m;
        if (top_m > max_m) max_m = top_m;
    }
    for (const auto & wl : p.wind_layers) {
        if (!wl.weather_enable) continue;
        if (wl.altitude_msl_m > max_m) max_m = wl.altitude_msl_m;
    }
    if (max_m <= 0.0f) {
        return static_cast<float>(XPLM_DEFAULT_WXR_LIMIT_MSL_FT);
    }
    return meters_to_ft(max_m) + 2000.0f;
}

// Probe X-Plane terrain for MSL elevation (meters) at the given lat/lon.
// Reuses the long-lived g_terrain_probe created at XPluginStart — same pattern
// as HOT response code. Returns 0.0 on probe miss (logged).
static double probe_terrain_elevation_m(double lat_deg, double lon_deg) {
    if (!g_terrain_probe) return 0.0;

    double local_x, local_y, local_z;
    XPLMWorldToLocal(lat_deg, lon_deg, 0.0, &local_x, &local_y, &local_z);

    XPLMProbeInfo_t info;
    info.structSize = sizeof info;
    XPLMProbeResult result = XPLMProbeTerrainXYZ(
        g_terrain_probe,
        static_cast<float>(local_x),
        static_cast<float>(local_y),
        static_cast<float>(local_z),
        &info);

    if (result != xplm_ProbeHitTerrain) {
        char msg[128];
        snprintf(msg, sizeof msg,
            "xplanecigi: terrain probe miss at %.6f,%.6f — using 0.0\n",
            lat_deg, lon_deg);
        XPLMDebugString(msg);
        return 0.0;
    }

    double out_lat, out_lon, out_alt_m;
    XPLMLocalToWorld(info.locationX, info.locationY, info.locationZ,
                     &out_lat, &out_lon, &out_alt_m);
    return out_alt_m;
}

// ─────────────────────────────────────────────────────────────────────────────
// Build an XPLMWeatherInfo_t for a patch using Option B overlay semantics:
//   - Seed from global pending_wx (read-only) so unauthored patch fields are
//     no-ops in X-Plane's weather blend.
//   - Overlay patch-authored fields where weather_enable=true.
//
// Units at boundaries:
//   pending_wx (global, existing):  coverage 0-1, pressure hPa, cloud_type XP 0-3
//   PendingPatch (regional, 3a):    coverage_pct 0-100, cloud_type CIGI 0-15
//   XPLMWeatherInfo_t (XP SDK):     coverage 0-1, pressure_sl Pa, cloud_type XP 0-3
//
// Regional cloud_type is remapped via remap_cloud_type() because 3a stores
// the raw CIGI wire enum (0-15). pending_wx already stores the XP enum (0-3).
// ─────────────────────────────────────────────────────────────────────────────
static XPLMWeatherInfo_t build_weather_info(const PendingPatch & p)
{
    XPLMWeatherInfo_t info = {};
    info.structSize = sizeof info;

    // 1. Seed from global pending_wx (no unit conversion for clouds — already
    //    XP-native units; pressure hPa → Pa).
    for (int i = 0; i < XPLM_NUM_CLOUD_LAYERS; ++i) {
        info.cloud_layers[i].cloud_type = pending_wx.cloud[i].type_xp;
        info.cloud_layers[i].coverage   = pending_wx.cloud[i].coverage;
        info.cloud_layers[i].alt_base   = pending_wx.cloud[i].base_m;
        info.cloud_layers[i].alt_top    = pending_wx.cloud[i].top_m;
    }
    for (int i = 0; i < XPLM_NUM_WIND_LAYERS; ++i) {
        info.wind_layers[i].alt_msl   = pending_wx.wind[i].alt_m;
        info.wind_layers[i].speed     = pending_wx.wind[i].speed_ms;
        info.wind_layers[i].direction = pending_wx.wind[i].dir_deg;
        // gust_speed / shear / turbulence left at zero; patch may overlay via
        // future extensions (not currently wired).
    }
    info.visibility      = pending_wx.visibility_m;
    info.temperature_alt = pending_wx.temperature_c;
    info.pressure_sl     = pending_wx.pressure_hpa * 100.0f;   // hPa → Pa
    info.pressure_alt    = 0.0f;  // 0 signals "use pressure_sl"
    info.precip_rate     = pending_wx.rain_pct;

    // 2. Overlay patch-authored cloud layers (framework layer_id 1-3 → slots 0-2).
    //    coverage_pct is 0-100 on the wire; divide by 100. cloud_type is CIGI
    //    raw (0-15); remap to XP enum (0-3).
    for (const auto & cl : p.cloud_layers) {
        if (!cl.weather_enable) continue;
        if (cl.layer_id < 1 || cl.layer_id > 3) continue;
        int slot = cl.layer_id - 1;
        info.cloud_layers[slot].cloud_type = remap_cloud_type(cl.cloud_type);
        info.cloud_layers[slot].coverage   = cl.coverage_pct / 100.0f;
        info.cloud_layers[slot].alt_base   = cl.base_elevation_m;
        info.cloud_layers[slot].alt_top    = cl.base_elevation_m + cl.thickness_m;
    }

    // 3. Overlay patch-authored wind layers (framework layer_id 10-12 → slots 0-2).
    for (const auto & wl : p.wind_layers) {
        if (!wl.weather_enable) continue;
        if (wl.layer_id < 10 || wl.layer_id > 12) continue;
        int slot = wl.layer_id - 10;
        info.wind_layers[slot].alt_msl   = wl.altitude_msl_m;
        info.wind_layers[slot].speed     = wl.wind_speed_ms;
        info.wind_layers[slot].direction = wl.wind_direction_deg;
    }

    // 4. Overlay scalar overrides (visibility + temperature at layer 20,
    //    precipitation at layer 21). Precipitation fields currently stored as
    //    NaN by 3a decoder (encoder doesn't carry them yet) — overlay is a
    //    no-op until that path lands.
    for (const auto & ov : p.scalar_overrides) {
        if (!ov.weather_enable) continue;
        if (ov.layer_id == 20) {
            if (!std::isnan(ov.visibility_m))  info.visibility      = ov.visibility_m;
            if (!std::isnan(ov.temperature_c)) info.temperature_alt = ov.temperature_c;
        } else if (ov.layer_id == 21) {
            if (!std::isnan(ov.precipitation_rate)) {
                info.precip_rate = ov.precipitation_rate;
            }
            // precipitation_type not yet surfaced in XPLMWeatherInfo_t;
            // X-Plane derives rain/snow from temperature.
        }
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// Weather flight loop: write decoded CIGI weather to X-Plane datarefs (~1 Hz)
// ─────────────────────────────────────────────────────────────────────────────
static float WeatherFlightLoopCb(float, float, int, void *)
{
    // Global weather writes — gated on pending_wx.dirty (existing behavior).
    if (pending_wx.dirty) {

    if (dr_update_immediately)
        XPLMSetDatai(dr_update_immediately, 1);

    // Global atmosphere
    if (dr_sealevel_temp_c)
        XPLMSetDataf(dr_sealevel_temp_c, pending_wx.temperature_c);
    if (dr_sealevel_pressure_pas)
        XPLMSetDataf(dr_sealevel_pressure_pas, pending_wx.pressure_hpa * 100.0f);
    if (dr_visibility_sm)
        XPLMSetDataf(dr_visibility_sm, pending_wx.visibility_m / 1609.34f);
    if (dr_rain_percent)
        XPLMSetDataf(dr_rain_percent, pending_wx.rain_pct);

    // Cloud layers [3]
    float cloud_base[3] = {}, cloud_top[3] = {}, cloud_cov[3] = {}, cloud_tp[3] = {};
    for (int i = 0; i < 3; i++) {
        if (pending_wx.cloud[i].valid) {
            cloud_base[i] = pending_wx.cloud[i].base_m;
            cloud_top[i]  = pending_wx.cloud[i].top_m;
            cloud_cov[i]  = pending_wx.cloud[i].coverage;
            cloud_tp[i]   = pending_wx.cloud[i].type_xp;
        }
    }
    if (dr_cloud_base)     XPLMSetDatavf(dr_cloud_base,     cloud_base, 0, 3);
    if (dr_cloud_tops)     XPLMSetDatavf(dr_cloud_tops,     cloud_top,  0, 3);
    if (dr_cloud_coverage) XPLMSetDatavf(dr_cloud_coverage, cloud_cov,  0, 3);
    if (dr_cloud_type)     XPLMSetDatavf(dr_cloud_type,     cloud_tp,   0, 3);

    // Wind layers [13]
    float w_alt[13]={}, w_dir[13]={}, w_spd[13]={}, w_turb[13]={};
    int valid_winds = 0;
    for (int i = 0; i < 13; i++) {
        if (pending_wx.wind[i].valid) {
            w_alt[i]  = pending_wx.wind[i].alt_m;
            w_dir[i]  = pending_wx.wind[i].dir_deg;
            w_spd[i]  = pending_wx.wind[i].speed_ms / 0.51444f;  // m/s → kts
            w_turb[i] = pending_wx.wind[i].turb * 10.0f;         // 0-1 → 0-10
            valid_winds++;
        }
    }
    if (valid_winds > 0) {
        if (dr_wind_alt)  XPLMSetDatavf(dr_wind_alt,  w_alt,  0, 13);
        if (dr_wind_dir)  XPLMSetDatavf(dr_wind_dir,  w_dir,  0, 13);
        if (dr_wind_spd)  XPLMSetDatavf(dr_wind_spd,  w_spd,  0, 13);
        if (dr_wind_turb) XPLMSetDatavf(dr_wind_turb, w_turb, 0, 13);
    }

    // Runway friction — direct passthrough (XP enum matches our 0-15).
    // Dataref is typed float, not int.
    if (dr_runway_friction)
        XPLMSetDataf(dr_runway_friction, static_cast<float>(pending_wx.runway_friction));

    if (dr_update_immediately)
        XPLMSetDatai(dr_update_immediately, 0);

    // Cloud/precipitation changes need a regen to push the edit through
    // X-Plane's weather pipeline (dataref writes alone don't update visuals fast).
    // Wind/visibility/temperature changes apply immediately — no regen needed.
    // Gated by g_regen_mode config so instructors can trade responsiveness for
    // smoothness in flight.
    if (pending_wx.cloud_changed && cmd_regen_weather) {
        bool fire = false;
        switch (g_regen_mode) {
            case RegenMode::Always:     fire = true;  break;
            case RegenMode::Never:      fire = false; break;
            case RegenMode::GroundOnly:
                fire = dr_onground_any
                    && XPLMGetDatai(dr_onground_any) != 0;
                break;
        }
        if (fire) XPLMCommandOnce(cmd_regen_weather);
        pending_wx.cloud_changed = false;
    }

    // Log once on change
    char dbg[256];
    snprintf(dbg, sizeof(dbg),
        "xplanecigi: WX applied — vis=%.0fm temp=%.1fC wind=%03.0f/%.1fms clouds=%d rain=%.0f%% rwy_fric=%u\n",
        pending_wx.visibility_m, pending_wx.temperature_c,
        pending_wx.wind_dir_deg, pending_wx.wind_speed_ms,
        (int)(pending_wx.cloud[0].valid + pending_wx.cloud[1].valid + pending_wx.cloud[2].valid),
        pending_wx.rain_pct * 100.0f,
        (unsigned)pending_wx.runway_friction);
    XPLMDebugString(dbg);

    pending_wx.dirty = false;
    }  // end if (pending_wx.dirty) — global weather writes

    // ── Slice 3b: Regional weather patch apply / erase ──────────────────────
    // Runs every 1 Hz tick regardless of pending_wx.dirty. Self-guarded by
    // the outer if — both maps empty → no work, no SDK calls.
    //
    // Batch all Set/Erase in a single Begin/End(isIncremental=1,
    // updateImmediately=1) context so instructor changes take effect right
    // away rather than morphing over minutes.
    if (!g_pending_patches.empty() || !g_xp_applied.empty()) {
        struct ApplyJob { uint16_t id; double lat, lon; double ground_m; XPLMWeatherInfo_t info; float radius_nm; };
        struct EraseJob { uint16_t id; double lat, lon; };
        std::vector<ApplyJob> applies;
        std::vector<EraseJob> erases;

        // 1. Regions in applied but not pending → erase
        for (auto it = g_xp_applied.begin(); it != g_xp_applied.end(); ) {
            if (g_pending_patches.find(it->first) == g_pending_patches.end()) {
                erases.push_back({ it->first, it->second.lat_deg, it->second.lon_deg });
                it = g_xp_applied.erase(it);
            } else {
                ++it;
            }
        }

        // 2. New or moved or dirty pending patches → apply
        for (auto & kv : g_pending_patches) {
            uint16_t id = kv.first;
            PendingPatch & p = kv.second;
            auto applied_it = g_xp_applied.find(id);

            bool needs_apply = false;
            if (applied_it == g_xp_applied.end()) {
                needs_apply = true;
            } else {
                bool moved = (applied_it->second.lat_deg != p.lat_deg) ||
                             (applied_it->second.lon_deg != p.lon_deg);
                if (moved) {
                    erases.push_back({ id, applied_it->second.lat_deg, applied_it->second.lon_deg });
                    needs_apply = true;
                } else if (p.dirty) {
                    needs_apply = true;
                }
            }

            if (needs_apply) {
                ApplyJob job;
                job.id       = id;
                job.lat      = p.lat_deg;
                job.lon      = p.lon_deg;
                job.ground_m = probe_terrain_elevation_m(p.lat_deg, p.lon_deg);
                job.info     = build_weather_info(p);
                job.info.radius_nm           = meters_to_nm(p.corner_radius_m);
                job.info.max_altitude_msl_ft = derive_max_alt_ft(p);
                job.radius_nm = job.info.radius_nm;
                applies.push_back(job);
                p.dirty = false;
            }
        }

        // 3. Issue SDK calls in a single Begin/End context.
        if (!applies.empty() || !erases.empty()) {
            XPLMBeginWeatherUpdate();

            for (const auto & e : erases) {
                XPLMEraseWeatherAtLocation(e.lat, e.lon);
                char msg[160];
                snprintf(msg, sizeof msg,
                    "xplanecigi: erased region %u at %.6f,%.6f\n",
                    e.id, e.lat, e.lon);
                XPLMDebugString(msg);
            }
            for (auto & a : applies) {
                XPLMSetWeatherAtLocation(a.lat, a.lon, a.ground_m, &a.info);
                g_xp_applied[a.id] = { a.id, a.lat, a.lon, a.radius_nm };
                char msg[192];
                snprintf(msg, sizeof msg,
                    "xplanecigi: applied region %u at %.6f,%.6f ground=%.0fm radius=%.1fnm\n",
                    a.id, a.lat, a.lon, a.ground_m, a.radius_nm);
                XPLMDebugString(msg);
            }

            XPLMEndWeatherUpdate(/*isIncremental=*/1, /*updateImmediately=*/1);
        }
    }

    return 1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// X-Plane flight loop: drain all pending UDP datagrams each frame
// ─────────────────────────────────────────────────────────────────────────────
static float FlightLoopCallback(float, float, int, void *)
{
    if (g_recv_sock == INVALID_SOCKET) return -1.0f;

    // Terrain probe stability check — runs after Reset → Operate transition.
    // Probes at entity position every 0.5s; 4 consecutive within 1m = stable.
    // SOF reports Standby during probing, Normal after stable.
    if (g_probing_terrain && g_terrain_probe) {
        double now = XPLMGetElapsedTime();
        if (now - g_last_probe_t >= PROBE_INTERVAL_S) {
            g_last_probe_t = now;

            double lx, ly, lz;
            XPLMWorldToLocal(g_prev_lat, g_prev_lon, 0.0, &lx, &ly, &lz);
            XPLMProbeInfo_t info;
            info.structSize = sizeof(info);
            XPLMProbeResult result = XPLMProbeTerrainXYZ(
                g_terrain_probe, (float)lx, 0.0f, (float)lz, &info);

            if (result == xplm_ProbeHitTerrain) {
                double lat, lon, alt;
                XPLMLocalToWorld(info.locationX, info.locationY, info.locationZ,
                                 &lat, &lon, &alt);
                double delta = std::abs(alt - g_last_probe_alt);
                if (delta < PROBE_TOLERANCE_M) {
                    g_probe_stable_count++;
                } else {
                    g_probe_stable_count = 0;
                }

                char dbg[256];
                snprintf(dbg, sizeof(dbg),
                    "xplanecigi: probe alt=%.1fm delta=%.2fm stable=%d/%d\n",
                    alt, delta, g_probe_stable_count, PROBE_STABLE_COUNT);
                XPLMDebugString(dbg);

                g_last_probe_alt = alt;

                if (g_probe_stable_count >= PROBE_STABLE_COUNT) {
                    g_probing_terrain = false;
                    snprintf(dbg, sizeof(dbg),
                        "xplanecigi: terrain stable at %.1fm MSL — SOF Normal\n", alt);
                    XPLMDebugString(dbg);
                }
            }
        }
    }

    static uint8_t buf[65536];
    int len;
    while ((len = recv(g_recv_sock,
                       reinterpret_cast<char *>(buf),
                       (int)sizeof(buf), 0)) > 0)
    {
        process_datagram(buf, len);
    }
    return -1.0f;  // reschedule every frame
}

// ─────────────────────────────────────────────────────────────────────────────
// Plugin lifecycle
// ─────────────────────────────────────────────────────────────────────────────
PLUGIN_API int XPluginStart(char * outName, char * outSig, char * outDesc)
{
    strcpy(outName, "xplanecigi");
    strcpy(outSig,  "sim.cigi.xplanecigi");
    strcpy(outDesc, "CIGI 3.3 raw IG plugin — driven by sim_cigi_bridge (ROS2)");

    load_config();

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        XPLMDebugString("xplanecigi: WSAStartup failed\n");
        return 0;
    }

    // ── Receive socket (bind on IG_PORT, non-blocking) ────────────────────
    g_recv_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_recv_sock == INVALID_SOCKET) {
        XPLMDebugString("xplanecigi: recv socket() failed\n");
        WSACleanup();
        return 0;
    }
    struct sockaddr_in bind_addr {};
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port        = htons(g_ig_port);
    if (bind(g_recv_sock,
             reinterpret_cast<struct sockaddr *>(&bind_addr),
             sizeof(bind_addr)) != 0) {
        XPLMDebugString("xplanecigi: bind() failed\n");
        closesocket(g_recv_sock); g_recv_sock = INVALID_SOCKET;
        WSACleanup();
        return 0;
    }
    u_long nb = 1;
    ioctlsocket(g_recv_sock, FIONBIO, &nb);

    // ── Send socket (sendto HOST_IP:HOST_PORT) ────────────────────────────
    g_send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_send_sock == INVALID_SOCKET) {
        XPLMDebugString("xplanecigi: send socket() failed\n");
        closesocket(g_recv_sock); g_recv_sock = INVALID_SOCKET;
        WSACleanup();
        return 0;
    }
    std::memset(&g_host_addr, 0, sizeof(g_host_addr));
    g_host_addr.sin_family = AF_INET;
    g_host_addr.sin_port   = htons(g_host_port);
    InetPtonA(AF_INET, g_host_ip, &g_host_addr.sin_addr);

    // ── DataRefs ──────────────────────────────────────────────────────────
    g_local_x = XPLMFindDataRef("sim/flightmodel/position/local_x");
    g_local_y = XPLMFindDataRef("sim/flightmodel/position/local_y");
    g_local_z = XPLMFindDataRef("sim/flightmodel/position/local_z");
    g_phi     = XPLMFindDataRef("sim/flightmodel/position/phi");
    g_theta   = XPLMFindDataRef("sim/flightmodel/position/theta");
    g_psi     = XPLMFindDataRef("sim/flightmodel/position/psi");

    // Create terrain probe for HOT requests
    g_terrain_probe = XPLMCreateProbe(xplm_ProbeY);

    // Surface type under aircraft CG (for HAT response).
    // X-Plane has two candidate datarefs in different versions — probe both.
    g_surface_type = XPLMFindDataRef("sim/flightmodel/ground/surface_texture_type");
    if (!g_surface_type) {
        g_surface_type = XPLMFindDataRef("sim/flightmodel2/ground/surface_texture_type");
    }
    XPLMDebugString(g_surface_type
        ? "xplanecigi: surface_type dataref bound\n"
        : "xplanecigi: WARNING — no surface_type dataref found, HAT surface=UNKNOWN\n");

    // Weather region datarefs (writable since XP 12.0)
    dr_sealevel_temp_c       = XPLMFindDataRef("sim/weather/region/sealevel_temperature_c");
    dr_sealevel_pressure_pas = XPLMFindDataRef("sim/weather/region/sealevel_pressure_pas");
    dr_visibility_sm         = XPLMFindDataRef("sim/weather/region/visibility_reported_sm");
    dr_rain_percent          = XPLMFindDataRef("sim/weather/region/rain_percent");
    dr_cloud_base            = XPLMFindDataRef("sim/weather/region/cloud_base_msl_m");
    dr_cloud_tops            = XPLMFindDataRef("sim/weather/region/cloud_tops_msl_m");
    dr_cloud_coverage        = XPLMFindDataRef("sim/weather/region/cloud_coverage_percent");
    dr_cloud_type            = XPLMFindDataRef("sim/weather/region/cloud_type");
    dr_wind_alt              = XPLMFindDataRef("sim/weather/region/wind_altitude_msl_m");
    dr_wind_dir              = XPLMFindDataRef("sim/weather/region/wind_direction_degt");
    dr_wind_spd              = XPLMFindDataRef("sim/weather/region/wind_speed_msc");
    dr_wind_turb             = XPLMFindDataRef("sim/weather/region/turbulence");
    dr_runway_friction       = XPLMFindDataRef("sim/weather/region/runway_friction");
    dr_update_immediately    = XPLMFindDataRef("sim/weather/region/update_immediately");
    dr_onground_any          = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    cmd_regen_weather        = XPLMFindCommand("sim/operation/regen_weather");

    {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
            "xplanecigi: regen_weather = %s\n", regen_mode_name(g_regen_mode));
        XPLMDebugString(msg);
    }

    // Register weather flight loop (1 Hz, writes datarefs only when dirty)
    XPLMCreateFlightLoop_t wx_fl;
    wx_fl.structSize   = sizeof(wx_fl);
    wx_fl.phase        = xplm_FlightLoop_Phase_BeforeFlightModel;
    wx_fl.callbackFunc = WeatherFlightLoopCb;
    wx_fl.refcon       = nullptr;
    weather_flight_loop_id = XPLMCreateFlightLoop(&wx_fl);
    XPLMScheduleFlightLoop(weather_flight_loop_id, 1.0f, true);

    // Override X-Plane's FDM so it doesn't overwrite our position each frame
    g_override_planepath = XPLMFindDataRef("sim/operation/override/override_planepath");
    if (g_override_planepath) {
        int one = 1;
        XPLMSetDatavi(g_override_planepath, &one, 0, 1);
        XPLMDebugString("xplanecigi: override_planepath[0] = 1 (FDM disabled)\n");
    } else {
        XPLMDebugString("xplanecigi: WARNING — override_planepath dataref not found\n");
    }

    XPLMRegisterFlightLoopCallback(FlightLoopCallback, -1.0f, nullptr);

    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "xplanecigi: listening on :%u, sending SOF to %s:%u\n",
                  (unsigned)g_ig_port, g_host_ip, (unsigned)g_host_port);
    XPLMDebugString(msg);
    return 1;
}

PLUGIN_API void XPluginStop()
{
    // Restore X-Plane's own FDM
    if (g_override_planepath) {
        int zero = 0;
        XPLMSetDatavi(g_override_planepath, &zero, 0, 1);
    }

    XPLMUnregisterFlightLoopCallback(FlightLoopCallback, nullptr);
    if (weather_flight_loop_id) {
        XPLMDestroyFlightLoop(weather_flight_loop_id);
        weather_flight_loop_id = nullptr;
    }
    if (g_terrain_probe) { XPLMDestroyProbe(g_terrain_probe); g_terrain_probe = nullptr; }
    if (g_recv_sock != INVALID_SOCKET) { closesocket(g_recv_sock); g_recv_sock = INVALID_SOCKET; }
    if (g_send_sock != INVALID_SOCKET) { closesocket(g_send_sock); g_send_sock = INVALID_SOCKET; }
    WSACleanup();
}

PLUGIN_API int  XPluginEnable()                                  { return 1; }
PLUGIN_API void XPluginDisable()                                 {}
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void *) {}
