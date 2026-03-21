// X-Plane CIGI HOT terrain probe plugin
// Listens for CIGI 3.3 HOT Request packets (0x18) from the simulator host,
// probes terrain using XPLMProbeTerrainXYZ, and returns HOT Response packets (0x02).
//
// Build:  Requires X-Plane SDK (XPLM headers).
//         Compile as a shared library (.xpl) and install in X-Plane Resources/plugins/.
//         This file is NOT built by the ROS2 colcon workspace.
//
// UDP ports (must match cigi_bridge config):
//   Listen on 8002 (ig_port)  — receives from host
//   Send to   8001 (host_port) — sends responses

#if defined(XPLM200) || defined(XPLM300) || defined(XPLM400)

#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMScenery.h>
#include <XPLMGraphics.h>
#include <XPLMUtilities.h>

#include <cstring>
#include <cstdint>
#include <cmath>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// ── Configuration ────────────────────────────────────────────────────────────
static constexpr int IG_LISTEN_PORT = 8002;
static constexpr int HOST_PORT      = 8001;
static constexpr const char * HOST_ADDR = "127.0.0.1";

// ── Globals ──────────────────────────────────────────────────────────────────
static int g_recv_fd = -1;
static int g_send_fd = -1;
static struct sockaddr_in g_host_addr {};
static XPLMProbeRef g_probe = nullptr;

// ── Big-endian helpers ───────────────────────────────────────────────────────
static uint16_t read_be16(const uint8_t * p)
{
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

static double read_be_double(const uint8_t * p)
{
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u = (u << 8) | p[i];
    double v;
    memcpy(&v, &u, 8);
    return v;
}

static void write_be16(uint8_t * p, uint16_t v)
{
    p[0] = (v >> 8) & 0xFF;
    p[1] =  v       & 0xFF;
}

static void write_be_double(uint8_t * p, double v)
{
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    p[0] = (u >> 56) & 0xFF; p[1] = (u >> 48) & 0xFF;
    p[2] = (u >> 40) & 0xFF; p[3] = (u >> 32) & 0xFF;
    p[4] = (u >> 24) & 0xFF; p[5] = (u >> 16) & 0xFF;
    p[6] = (u >>  8) & 0xFF; p[7] =  u         & 0xFF;
}

// ── HOT processing ──────────────────────────────────────────────────────────
static void process_hot_request(const uint8_t * pkt, int size)
{
    if (size < 32) return;

    uint16_t request_id  = read_be16(&pkt[2]);
    double   lat_deg     = read_be_double(&pkt[12]);
    double   lon_deg     = read_be_double(&pkt[20]);

    // Convert geodetic to X-Plane local coordinates
    double local_x, local_y, local_z;
    XPLMWorldToLocal(lat_deg, lon_deg, 0.0, &local_x, &local_y, &local_z);

    // Probe terrain
    XPLMProbeInfo_t info;
    info.structSize = sizeof(info);
    XPLMProbeResult result = XPLMProbeTerrainXYZ(
        g_probe,
        static_cast<float>(local_x),
        0.0f,
        static_cast<float>(local_z),
        &info);

    bool valid = (result == xplm_ProbeHitTerrain);
    double hot_msl = 0.0;

    if (valid) {
        // Convert terrain local Y back to MSL
        double out_lat, out_lon, out_alt;
        XPLMLocalToWorld(info.locationX, info.locationY, info.locationZ,
                         &out_lat, &out_lon, &out_alt);
        hot_msl = out_alt;
    }

    // Build HOT Response packet (0x02, 48 bytes)
    uint8_t resp[48];
    memset(resp, 0, sizeof resp);
    resp[0] = 0x02;          // Packet ID = HOT Response
    resp[1] = 48;            // Packet Size
    write_be16(&resp[2], request_id);
    resp[4] = valid ? 0x01 : 0x00;   // Valid flag
    write_be_double(&resp[8], hot_msl);       // HOT (terrain MSL, metres)
    write_be_double(&resp[16], 0.0);          // HAT placeholder (host computes)

    sendto(g_send_fd, resp, 48, 0,
           reinterpret_cast<struct sockaddr *>(&g_host_addr), sizeof(g_host_addr));
}

// ── Flight loop callback ────────────────────────────────────────────────────
static float flight_loop_cb(float, float, int, void *)
{
    if (g_recv_fd < 0) return 1.0f;

    uint8_t buf[4096];
    while (true) {
        ssize_t n = recv(g_recv_fd, reinterpret_cast<char*>(buf), sizeof(buf), 0);
        if (n <= 0) break;

        // Walk CIGI packet stream
        ssize_t offset = 0;
        while (offset + 2 <= n) {
            uint8_t pkt_id   = buf[offset];
            uint8_t pkt_size = buf[offset + 1];
            if (pkt_size < 2 || offset + pkt_size > n) break;

            if (pkt_id == 0x18) {
                // HOT Request
                process_hot_request(&buf[offset], pkt_size);
            }
            // IG Control (0x01) and Entity Control (0x03) handled elsewhere

            offset += pkt_size;
        }
    }

    return -1.0f;  // called every frame
}

// ── Plugin entry points ─────────────────────────────────────────────────────
PLUGIN_API int XPluginStart(char * name, char * sig, char * desc)
{
    strcpy(name, "CIGI HOT Terrain");
    strcpy(sig,  "sim.cigi.hot_terrain");
    strcpy(desc, "Handles CIGI HOT terrain probe requests via XPLMProbeTerrainXYZ");

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    g_probe = XPLMCreateProbe(xplm_ProbeY);

    // Receive socket — listens for CIGI packets from host
    g_recv_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_recv_fd >= 0) {
        struct sockaddr_in bind_addr {};
        bind_addr.sin_family      = AF_INET;
        bind_addr.sin_addr.s_addr = INADDR_ANY;
        bind_addr.sin_port        = htons(IG_LISTEN_PORT);
        if (bind(g_recv_fd, reinterpret_cast<struct sockaddr *>(&bind_addr), sizeof bind_addr) < 0) {
            XPLMDebugString("CIGI HOT: bind failed on port 8002\n");
#ifdef _WIN32
            closesocket(g_recv_fd);
#else
            close(g_recv_fd);
#endif
            g_recv_fd = -1;
        } else {
            // Set non-blocking
#ifdef _WIN32
            u_long mode = 1;
            ioctlsocket(g_recv_fd, FIONBIO, &mode);
#else
            fcntl(g_recv_fd, F_SETFL, fcntl(g_recv_fd, F_GETFL, 0) | O_NONBLOCK);
#endif
        }
    }

    // Send socket — responses go to host
    g_send_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    memset(&g_host_addr, 0, sizeof g_host_addr);
    g_host_addr.sin_family = AF_INET;
    g_host_addr.sin_port   = htons(HOST_PORT);
    inet_aton(HOST_ADDR, &g_host_addr.sin_addr);

    return 1;
}

PLUGIN_API void XPluginStop()
{
    if (g_probe) { XPLMDestroyProbe(g_probe); g_probe = nullptr; }
#ifdef _WIN32
    if (g_recv_fd >= 0) closesocket(g_recv_fd);
    if (g_send_fd >= 0) closesocket(g_send_fd);
    WSACleanup();
#else
    if (g_recv_fd >= 0) close(g_recv_fd);
    if (g_send_fd >= 0) close(g_send_fd);
#endif
    g_recv_fd = -1;
    g_send_fd = -1;
}

PLUGIN_API int XPluginEnable()
{
    XPLMRegisterFlightLoopCallback(flight_loop_cb, -1.0f, nullptr);
    XPLMDebugString("CIGI HOT Terrain: enabled\n");
    return 1;
}

PLUGIN_API void XPluginDisable()
{
    XPLMUnregisterFlightLoopCallback(flight_loop_cb, nullptr);
    XPLMDebugString("CIGI HOT Terrain: disabled\n");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void *) {}

#endif // XPLM guard
