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

#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include <fstream>

// ── Network config (loaded from xplanecigi.ini, fallback to defaults) ─────────
static char     g_host_ip[64] = "127.0.0.1";
static uint16_t g_ig_port     = 8002;
static uint16_t g_host_port   = 8001;

// Read config from <plugin_dir>/xplanecigi.ini
// Format (all optional, unknown keys ignored):
//   host_ip=192.168.1.100
//   ig_port=8002
//   host_port=8001
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
    }

    char msg[256];
    std::snprintf(msg, sizeof(msg), "xplanecigi: config loaded from %s\n", config_path.c_str());
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

            // Send HOT Response (0x02, 48 bytes)
            uint8_t resp[48] = {};
            resp[0] = 0x02;        // Packet ID = HAT/HOT Response
            resp[1] = 48;          // Packet Size
            write_be16(resp + 2, req_id);
            resp[4] = valid ? 0x01 : 0x00;         // Valid flag
            write_be_double(resp + 8, hot_msl);     // HOT (terrain MSL, metres)
            write_be_double(resp + 16, 0.0);        // HAT placeholder

            sendto(g_send_sock,
                   reinterpret_cast<const char *>(resp), 48, 0,
                   reinterpret_cast<const struct sockaddr *>(&g_host_addr),
                   sizeof(g_host_addr));
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
    if (g_terrain_probe) { XPLMDestroyProbe(g_terrain_probe); g_terrain_probe = nullptr; }
    if (g_recv_sock != INVALID_SOCKET) { closesocket(g_recv_sock); g_recv_sock = INVALID_SOCKET; }
    if (g_send_sock != INVALID_SOCKET) { closesocket(g_send_sock); g_send_sock = INVALID_SOCKET; }
    WSACleanup();
}

PLUGIN_API int  XPluginEnable()                                  { return 1; }
PLUGIN_API void XPluginDisable()                                 {}
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void *) {}
