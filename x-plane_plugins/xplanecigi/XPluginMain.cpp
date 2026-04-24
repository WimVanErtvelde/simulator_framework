// XPluginMain.cpp — CIGI 3.3 IG plugin for X-Plane
//
// Datagram parsing + assembly flows through cigi_session::IgSession (a thin
// wrapper around Boeing's CigiIGSession). Incoming host packets dispatch to
// per-packet I*Processor hooks; outgoing SOF + HAT/HOT Extended Response
// emission goes through IgSession::BeginFrame → Append* → FinishFrame.
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

#include "cigi_session/IgSession.h"
#include "cigi_session/ComponentIds.h"
#include "cigi_session/processors/IIgCtrlProcessor.h"
#include "cigi_session/processors/IEntityCtrlProcessor.h"
#include "cigi_session/processors/IHatHotReqProcessor.h"
#include "cigi_session/processors/IAtmosphereProcessor.h"
#include "cigi_session/processors/IEnvRegionProcessor.h"
#include "cigi_session/processors/IWeatherCtrlProcessor.h"
#include "cigi_session/processors/ICompCtrlProcessor.h"

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

// regen_weather gating: "always" | "ground_only" | "ground_or_frozen" | "never"
enum class RegenMode { Always, GroundOnly, GroundOrFrozen, Never };
static RegenMode g_regen_mode = RegenMode::GroundOnly;

// Regional weather application mode. blend_at_ownship works around the
// XPLMSetWeatherAtLocation visible-rendering limitation (DECISIONS.md
// 2026-04-20) by computing the effective weather at ownship from global +
// active patches and writing the result to sim/weather/region/* datarefs.
// sdk_regional retains the spec-compliant per-patch SetWeatherAtLocation
// path for future SDK fix or non-XP IG integration.
enum class PluginWeatherMode { SdkRegional, BlendAtOwnship };
static PluginWeatherMode g_plugin_weather_mode = PluginWeatherMode::BlendAtOwnship;

// Host sim freeze state, driven by OnCompCtrl(class=System, id=SimFreezeState).
// True whenever the host isn't advancing the scene (instructor freeze or
// pending reposition). Used to gate regen_weather in ground_or_frozen mode —
// when frozen, the sim is already paused so a cloud regen isn't visible
// disruption.
static bool g_sim_frozen = false;

static const char * regen_mode_name(RegenMode m)
{
    switch (m) {
        case RegenMode::Always:         return "always";
        case RegenMode::GroundOnly:     return "ground_only";
        case RegenMode::GroundOrFrozen: return "ground_or_frozen";
        case RegenMode::Never:          return "never";
    }
    return "unknown";
}

static const char * plugin_weather_mode_name(PluginWeatherMode m)
{
    switch (m) {
        case PluginWeatherMode::SdkRegional:    return "sdk_regional";
        case PluginWeatherMode::BlendAtOwnship: return "blend_at_ownship";
    }
    return "unknown";
}

// Read config from <plugin_dir>/xplanecigi.ini
// Format (all optional, unknown keys ignored):
//   host_ip=192.168.1.100
//   ig_port=8002
//   host_port=8001
//   regen_weather=ground_or_frozen   (always | ground_only | ground_or_frozen | never)
//   plugin_weather_mode=blend_at_ownship  (blend_at_ownship | sdk_regional)
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
        char msg[384];
        std::snprintf(msg, sizeof(msg),
            "xplanecigi: config not found at %s — using defaults "
            "(127.0.0.1:%u, regen_weather=%s, plugin_weather_mode=%s)\n",
            config_path.c_str(), g_ig_port,
            regen_mode_name(g_regen_mode),
            plugin_weather_mode_name(g_plugin_weather_mode));
        XPLMDebugString(msg);
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
            if      (val == "always")           g_regen_mode = RegenMode::Always;
            else if (val == "ground_only")      g_regen_mode = RegenMode::GroundOnly;
            else if (val == "ground_or_frozen") g_regen_mode = RegenMode::GroundOrFrozen;
            else if (val == "never")            g_regen_mode = RegenMode::Never;
        }
        else if (key == "plugin_weather_mode") {
            if      (val == "blend_at_ownship") g_plugin_weather_mode = PluginWeatherMode::BlendAtOwnship;
            else if (val == "sdk_regional")     g_plugin_weather_mode = PluginWeatherMode::SdkRegional;
        }
    }

    char msg[256];
    std::snprintf(msg, sizeof(msg),
        "xplanecigi: config loaded from %s (regen_weather=%s, plugin_weather_mode=%s)\n",
        config_path.c_str(), regen_mode_name(g_regen_mode),
        plugin_weather_mode_name(g_plugin_weather_mode));
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
static XPLMDataRef g_elevation          = nullptr;  // double, ownship altitude MSL in metres

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
    // From Atmosphere Control
    float temperature_c = 15.0f;
    float visibility_m  = 9999.0f;
    float wind_speed_ms = 0.0f;
    float vert_wind_ms  = 0.0f;
    float wind_dir_deg  = 0.0f;
    float pressure_hpa  = 1013.25f;
    uint8_t humidity_pct = 0;
    float rain_pct      = 0.0f;
    uint8_t runway_condition_idx = 0;  // 0=Dry, 1-3=Wet, 4-6=Standing Water,
                                       // 7-9=Snow, 10-12=Ice, 13-15=Snow+Ice.
                                       // Forwarded to X-Plane's
                                       // sim/weather/region/runway_friction
                                       // dataref (X-Plane's naming, same
                                       // 0-15 index — kept for SDK compat).

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
// Accumulates Environmental Region Control geometry + Weather Control
// Scope=Regional layers into per-region structs. Slice 3b feeds these into
// XPLMSetWeatherAtLocation.
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
// ─────────────────────────────────────────────────────────────────────────────
struct XpAppliedRegion {
    uint16_t region_id;
    double   lat_deg;
    double   lon_deg;
    float    radius_nm;
};

static std::map<uint16_t, XpAppliedRegion> g_xp_applied;

// ─────────────────────────────────────────────────────────────────────────────
// Blend-at-ownship state (PluginWeatherMode::BlendAtOwnship).
// See README / DECISIONS.md 2026-04-23 for rationale. The blend fires when
// any of these conditions become true:
//   1. g_blend_dirty — set by region/weather-ctrl decoders on patch mutation,
//      by global pending_wx.dirty, or by OnCompCtrl (freeze transitions,
//      which affect regen gating, not the blend itself but we re-evaluate
//      anyway since it's cheap).
//   2. Ownship crossed a patch zone boundary (inside / transition / outside),
//      detected via per-patch g_patch_last_zone.
//   3. ≥5s since last blend AND moved >100m AND at least one patch has
//      weight > 0 — catches smooth in-transition updates when the aircraft
//      is crossing a transition zone slowly and coverage is changing
//      continuously.
//   4. Regen debounce timer expired (separate fire path, handled in the
//      flight loop; not a blend trigger).
// ─────────────────────────────────────────────────────────────────────────────

// Per-patch classification vs ownship.
enum class PatchZone : uint8_t { Outside = 0, Transition = 1, Inside = 2 };
static std::map<uint16_t, PatchZone> g_patch_last_zone;

// Set by patch mutations / global weather mutations. Cleared when the blend
// runs. "Dirty" does not guarantee the blend will actually fire — the 2s
// rate limit may defer it — but it guarantees the blend will run on the next
// permitted tick.
static bool   g_blend_dirty       = true;   // true on startup so first tick runs the blend
static double g_last_blend_time   = -1e9;   // wall time
static double g_last_blend_lat    = 0.0;
static double g_last_blend_lon    = 0.0;

// Rate limits. The blend cap prevents thrashing during fast transitions; the
// regen debounce is only to coalesce multiple cloud-layer mutations into one
// regen fire when the gate flips (e.g. instant of ground contact).
static constexpr double BLEND_MIN_INTERVAL_S       = 2.0;
static constexpr double REGEN_DEBOUNCE_S           = 2.0;
static constexpr double BLEND_SMOOTH_RECOMPUTE_S   = 5.0;
static constexpr double BLEND_SMOOTH_MIN_MOVE_M    = 100.0;

static double g_last_regen_time = -1e9;

// Forward declaration — defined below near build_weather_info.
static void cleanup_regional_weather();

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
static uint8_t g_ig_mode             = 2;       // from host: 0=Standby, 1=Operate, 2=Debug, 3=Offline
static bool    g_probing_terrain     = false;   // true while waiting for terrain stability
static double  g_last_probe_t        = 0.0;     // last probe wall time
static int     g_probe_stable_count  = 0;       // consecutive stable probes
// Ownship position from the most recent EntityCtrl (entity 0). Used by the
// terrain stability probe and (Step 5+) by the weather blend.
static double  g_ownship_lat         = 0.0;
static double  g_ownship_lon         = 0.0;
static double  g_last_probe_alt      = 0.0;

// Timing constants (seconds, frame-rate independent)
static constexpr double PROBE_INTERVAL_S   = 0.5;  // probe terrain every 0.5s
static constexpr int    PROBE_STABLE_COUNT = 4;     // 4 stable probes = 2s of stability
static constexpr double PROBE_TOLERANCE_M  = 1.0;   // probes within 1m = stable

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


// ─────────────────────────────────────────────────────────────────────────────
// IgSession + processor dispatch glue
// ─────────────────────────────────────────────────────────────────────────────
static cigi_session::IgSession g_ig_session;

// Send an SOF-led datagram (IG → Host). IgSession::BeginFrame appends the
// SOF packet automatically; caller may append additional packets (e.g.
// HAT/HOT Extended Response) before FinishFrame.
static void begin_ig_frame()
{
    // IG Mode wire value: 0=Standby (during probing), 1=Operate (ready).
    uint8_t ig_mode = g_probing_terrain ? 0 : 1;
    g_ig_session.BeginFrame(ig_mode, /*database_id=*/0,
                             g_ig_frame, g_host_frame);
}

static void finish_and_send_ig_frame()
{
    if (g_send_sock == INVALID_SOCKET) return;
    auto [buf, len] = g_ig_session.FinishFrame();
    if (!buf || len == 0) return;
    sendto(g_send_sock,
           reinterpret_cast<const char *>(buf), static_cast<int>(len), 0,
           reinterpret_cast<const struct sockaddr *>(&g_host_addr),
           sizeof(g_host_addr));
}

// Emit an SOF-only datagram (no extra packets). Matches the previous
// send_sof() behavior — one SOF after each received host datagram.
static void send_sof_only()
{
    begin_ig_frame();
    finish_and_send_ig_frame();
}

// ─────────────────────────────────────────────────────────────────────────────
// Processor implementations. Each OnXxx moves the body of the old
// process_packet() case block for the corresponding CIGI packet ID.
// ─────────────────────────────────────────────────────────────────────────────
class IgProcessor
  : public cigi_session::IIgCtrlProcessor,
    public cigi_session::IEntityCtrlProcessor,
    public cigi_session::IHatHotReqProcessor,
    public cigi_session::IAtmosphereProcessor,
    public cigi_session::IEnvRegionProcessor,
    public cigi_session::IWeatherCtrlProcessor,
    public cigi_session::ICompCtrlProcessor
{
public:
    // ── IG Control (host → IG) ──────────────────────────────────────────
    void OnIgCtrl(const cigi_session::IgCtrlFields & f) override
    {
        g_host_frame = f.host_frame_number;
        uint8_t new_mode = f.ig_mode;
        if (new_mode == g_ig_mode) return;

        char dbg[128];
        // CIGI 3.3 IG Mode enum: 0=Reset/Standby, 1=Operate, 2=Debug, 3=Offline
        static const char * mode_names[] = {"Standby", "Operate", "Debug", "Offline"};
        snprintf(dbg, sizeof(dbg), "xplanecigi: IG Mode %s → %s\n",
                 mode_names[g_ig_mode & 0x03], mode_names[new_mode & 0x03]);
        XPLMDebugString(dbg);

        // Standby → Operate transition: framework's reset/reposition
        // signal (host sends Standby for one frame, then Operate).
        // Begin terrain probe stability check.
        if (g_ig_mode == 0 && new_mode == 1) {
            g_probing_terrain = true;
            g_probe_stable_count = 0;
            g_last_probe_t = 0.0;
            g_last_probe_alt = 0.0;
            XPLMDebugString("xplanecigi: terrain probing started (Standby → Operate)\n");
        }
        // Entering Standby: host signals reposition or startup reset.
        // Erase all regional weather samples, clear tracking state,
        // trigger visual refresh.
        if (new_mode == 0 && g_ig_mode != 0) {
            XPLMDebugString("xplanecigi: entering Standby — cleaning up regional weather\n");
            cleanup_regional_weather();
        }
        g_ig_mode = new_mode;
    }

    // ── Entity Control (ownship position + attitude) ────────────────────
    void OnEntityCtrl(const cigi_session::EntityCtrlFields & f) override
    {
        // Convert geodetic → X-Plane local OpenGL coordinates
        double lx, ly, lz;
        XPLMWorldToLocal(f.lat_deg, f.lon_deg, f.alt_m, &lx, &ly, &lz);

        XPLMSetDatad(g_local_x, lx);
        XPLMSetDatad(g_local_y, ly);
        XPLMSetDatad(g_local_z, lz);

        // X-Plane sign convention for Euler angles:
        //   phi   (roll)  =  CIGI bank  (positive = right wing down)
        //   theta (pitch) =  CIGI pitch (positive = nose up)
        //   psi   (yaw)   =  CIGI heading
        XPLMSetDataf(g_phi,   f.roll_deg);
        XPLMSetDataf(g_theta, f.pitch_deg);
        XPLMSetDataf(g_psi,   f.yaw_deg);

        // Track ownship position for terrain probing and weather blend.
        g_ownship_lat = f.lat_deg;
        g_ownship_lon = f.lon_deg;
    }

    // ── HAT/HOT Request (host → IG) ──────────────────────────────────────
    void OnHatHotReq(const cigi_session::HatHotReqFields & f) override
    {
        if (!g_terrain_probe || g_send_sock == INVALID_SOCKET) return;

        // Convert geodetic to X-Plane local coords
        double lx, ly, lz;
        XPLMWorldToLocal(f.lat_deg, f.lon_deg, 0.0, &lx, &ly, &lz);

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

        // HAT = ownship altitude MSL − HOT at this gear point.
        // X-Plane's terrain probe API doesn't expose surface type per
        // (lat, lon), so we sample the enum under the aircraft CG and
        // assume it applies to all gear points.
        double  aircraft_alt = g_elevation ? XPLMGetDatad(g_elevation) : 0.0;
        double  hat_m        = aircraft_alt - hot_msl;
        uint8_t fw_surface   = 0;
        if (g_surface_type) {
            int xp_surface = XPLMGetDatai(g_surface_type);
            fw_surface = xp_surface_to_framework(xp_surface);
        }

        // Send SOF + HAT/HOT Extended Response as a single datagram.
        begin_ig_frame();
        g_ig_session.AppendHatHotXResp(
            f.request_id, valid, hat_m, hot_msl,
            static_cast<uint32_t>(fw_surface),
            /*normal_azimuth_deg=*/0.0f,
            /*normal_elevation_deg=*/0.0f);
        finish_and_send_ig_frame();
    }

    // ── Atmosphere Control (host → IG) ──────────────────────────────────
    void OnAtmosphere(const cigi_session::AtmosphereRxFields & f) override
    {
        // Clamp to sane ranges at the decoder so pending_wx is the
        // single source of truth. Values outside these ranges fall back
        // to ISA / sensible defaults — prevents NaN / division-by-zero
        // inside X-Plane's global weather pipeline when upstream
        // WeatherState messages arrive with unset (zero) globals.
        pending_wx.temperature_c =
            (std::isfinite(f.temperature_c) && f.temperature_c > -90.0f && f.temperature_c < 60.0f)
                ? f.temperature_c : 15.0f;
        pending_wx.visibility_m =
            (std::isfinite(f.visibility_m) && f.visibility_m > 100.0f)
                ? f.visibility_m : 10000.0f;
        pending_wx.wind_speed_ms =
            (std::isfinite(f.horiz_wind_ms) && f.horiz_wind_ms >= 0.0f && f.horiz_wind_ms < 150.0f)
                ? f.horiz_wind_ms : 0.0f;
        pending_wx.vert_wind_ms =
            (std::isfinite(f.vert_wind_ms) && f.vert_wind_ms > -50.0f && f.vert_wind_ms < 50.0f)
                ? f.vert_wind_ms : 0.0f;
        if (std::isfinite(f.wind_direction_deg)) {
            float wd = std::fmod(f.wind_direction_deg, 360.0f);
            if (wd < 0.0f) wd += 360.0f;
            pending_wx.wind_dir_deg = wd;
        } else {
            pending_wx.wind_dir_deg = 0.0f;
        }
        pending_wx.pressure_hpa =
            (std::isfinite(f.barometric_pressure_hpa) && f.barometric_pressure_hpa > 800.0f &&
             f.barometric_pressure_hpa < 1100.0f)
                ? f.barometric_pressure_hpa : 1013.25f;
        pending_wx.humidity_pct = f.humidity_pct;

        pending_wx.dirty = true;
        g_blend_dirty    = true;   // global changed → blend at ownship must re-evaluate
    }

    // ── Environmental Region Control (host → IG) ────────────────────────
    void OnEnvRegion(const cigi_session::EnvRegionFields & f) override
    {
        if (f.region_id == 0) {
            XPLMDebugString("xplanecigi: Env Region Ctrl with region_id=0 — ignoring\n");
            return;
        }

        if (f.region_state == 2) {
            // Destroyed — remove from pending map (Slice 3b fires XPLMEraseWeatherAtLocation)
            auto it = g_pending_patches.find(f.region_id);
            if (it != g_pending_patches.end()) {
                g_pending_patches.erase(it);
                g_patch_last_zone.erase(f.region_id);
                g_blend_dirty = true;
                char msg[128];
                snprintf(msg, sizeof msg,
                    "xplanecigi: Region %u destroyed (pending erase)\n", f.region_id);
                XPLMDebugString(msg);
            }
        } else {
            // Active (or Inactive — treat Inactive as Active for now; cigi_bridge
            // currently emits only Active or Destroyed).
            PendingPatch & p = g_pending_patches[f.region_id];
            p.region_id              = f.region_id;
            p.region_state           = f.region_state;
            p.lat_deg                = f.lat_deg;
            p.lon_deg                = f.lon_deg;
            p.size_x_m               = f.size_x_m;
            p.size_y_m               = f.size_y_m;
            p.corner_radius_m        = f.corner_radius_m;
            p.rotation_deg           = f.rotation_deg;
            p.transition_perimeter_m = f.transition_perimeter_m;
            p.dirty = true;
            g_blend_dirty = true;

            char msg[192];
            snprintf(msg, sizeof msg,
                "xplanecigi: Region %u state=%u lat=%.6f lon=%.6f radius=%.1fm transition=%.1fm\n",
                f.region_id, f.region_state, f.lat_deg, f.lon_deg,
                f.corner_radius_m, f.transition_perimeter_m);
            XPLMDebugString(msg);
        }
    }

    // ── Weather Control (host → IG) ──────────────────────────────────────
    void OnWeatherCtrl(const cigi_session::WeatherCtrlRxFields & f) override
    {
        if (f.scope == 1 /* Regional */) {
            if (f.region_id == 0) {
                XPLMDebugString("xplanecigi: Weather Ctrl Scope=Regional with region_id=0 — ignoring\n");
                return;
            }

            // cigi_bridge is supposed to emit Env Region Ctrl BEFORE any
            // Weather Control referencing that region. If the region is not
            // in g_pending_patches we still auto-create a stub — missing
            // geometry will be filled in on the next datagram.
            PendingPatch & p = g_pending_patches[f.region_id];
            p.region_id = f.region_id;
            p.dirty = true;
            g_blend_dirty = true;

            if (f.layer_id >= 1 && f.layer_id <= 3) {
                // Cloud layer slot
                PendingCloudLayer cl;
                cl.layer_id           = f.layer_id;
                cl.cloud_type         = f.cloud_type;
                cl.coverage_pct       = f.coverage_pct;
                cl.base_elevation_m   = f.base_elevation_m;
                cl.thickness_m        = f.thickness_m;
                cl.transition_band_m  = f.transition_band_m;
                cl.scud_enable        = f.scud_enable;
                cl.scud_frequency_pct = f.scud_frequency_pct;
                cl.weather_enable     = f.weather_enable;

                auto it = std::find_if(p.cloud_layers.begin(), p.cloud_layers.end(),
                    [&](const PendingCloudLayer & x){ return x.layer_id == f.layer_id; });
                if (it != p.cloud_layers.end()) *it = cl; else p.cloud_layers.push_back(cl);

                char msg[192];
                snprintf(msg, sizeof msg,
                    "xplanecigi: Region %u cloud layer %u type=%u cov=%.1f%% base=%.0fm thick=%.0fm en=%d\n",
                    f.region_id, f.layer_id, f.cloud_type,
                    cl.coverage_pct, cl.base_elevation_m, cl.thickness_m, f.weather_enable);
                XPLMDebugString(msg);
            } else if (f.layer_id >= 10 && f.layer_id <= 12) {
                // Wind layer slot
                PendingWindLayer wl;
                wl.layer_id           = f.layer_id;
                wl.altitude_msl_m     = f.base_elevation_m;
                wl.wind_speed_ms      = f.horiz_wind_ms;
                wl.vertical_wind_ms   = f.vert_wind_ms;
                wl.wind_direction_deg = f.wind_direction_deg;
                wl.weather_enable     = f.weather_enable;

                auto it = std::find_if(p.wind_layers.begin(), p.wind_layers.end(),
                    [&](const PendingWindLayer & x){ return x.layer_id == f.layer_id; });
                if (it != p.wind_layers.end()) *it = wl; else p.wind_layers.push_back(wl);

                char msg[192];
                snprintf(msg, sizeof msg,
                    "xplanecigi: Region %u wind layer %u alt=%.0fm spd=%.1fm/s dir=%.0fdeg en=%d\n",
                    f.region_id, f.layer_id, wl.altitude_msl_m,
                    wl.wind_speed_ms, wl.wind_direction_deg, f.weather_enable);
                XPLMDebugString(msg);
            } else if (f.layer_id == 20 || f.layer_id == 21) {
                // Scalar override: vis+temp (20) or precipitation (21)
                PendingScalarOverride ov;
                ov.layer_id           = f.layer_id;
                ov.temperature_c      = f.air_temp_c;
                ov.visibility_m       = f.visibility_m;
                ov.precipitation_rate = std::nanf("");
                ov.precipitation_type = 0;
                ov.weather_enable     = f.weather_enable;

                auto it = std::find_if(p.scalar_overrides.begin(), p.scalar_overrides.end(),
                    [&](const PendingScalarOverride & x){ return x.layer_id == f.layer_id; });
                if (it != p.scalar_overrides.end()) *it = ov; else p.scalar_overrides.push_back(ov);

                char msg[192];
                snprintf(msg, sizeof msg,
                    "xplanecigi: Region %u scalar override layer %u vis=%.0fm temp=%.1fC en=%d\n",
                    f.region_id, f.layer_id, ov.visibility_m, ov.temperature_c, f.weather_enable);
                XPLMDebugString(msg);
            } else {
                char msg[128];
                snprintf(msg, sizeof msg,
                    "xplanecigi: Region %u unknown layer_id %u — ignoring\n",
                    f.region_id, f.layer_id);
                XPLMDebugString(msg);
            }
            return;
        }

        if (f.scope == 0 /* Global */) {
            // Legacy behavior — write into global pending_wx state via
            // sim/weather/region/* datarefs.
            if (f.layer_id >= 1 && f.layer_id <= 3) {
                // Cloud layer — respect enable flag so host can disable a removed layer
                int idx = f.layer_id - 1;
                auto & slot = pending_wx.cloud[idx];
                if (f.weather_enable) {
                    float new_type = remap_cloud_type(f.cloud_type);
                    float new_cov  = f.coverage_pct / 100.0f;
                    float new_top  = f.base_elevation_m + f.thickness_m;
                    bool differs = !slot.valid
                        || slot.type_xp  != new_type
                        || slot.coverage != new_cov
                        || slot.base_m   != f.base_elevation_m
                        || slot.top_m    != new_top;
                    slot.type_xp  = new_type;
                    slot.coverage = new_cov;
                    slot.base_m   = f.base_elevation_m;
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
            } else if (!f.weather_enable) {
                return;  // precipitation/wind layers — legacy behavior
            } else if (f.layer_id == 4 || f.layer_id == 5) {
                // Precipitation
                float new_rain = f.coverage_pct / 100.0f;
                if (new_rain != pending_wx.rain_pct) pending_wx.cloud_changed = true;
                pending_wx.rain_pct = new_rain;
            } else if (f.layer_id >= 10) {
                // Wind-only layer
                int wind_idx = f.layer_id - 10;
                if (wind_idx < 13) {
                    pending_wx.wind[wind_idx].alt_m     = f.base_elevation_m;
                    pending_wx.wind[wind_idx].speed_ms  = f.horiz_wind_ms;
                    pending_wx.wind[wind_idx].dir_deg   = f.wind_direction_deg;
                    pending_wx.wind[wind_idx].vert_ms   = f.vert_wind_ms;
                    pending_wx.wind[wind_idx].turb      = f.severity / 5.0f;
                    pending_wx.wind[wind_idx].valid     = true;
                }
            }
            pending_wx.dirty = true;
            g_blend_dirty    = true;
            return;
        }

        // Scope 2 (Entity) or unknown — not used by this framework
        char msg[96];
        snprintf(msg, sizeof msg,
            "xplanecigi: Weather Ctrl unexpected scope=%u — ignoring\n", f.scope);
        XPLMDebugString(msg);
    }

    // ── Component Control (host → IG) ───────────────────────────────────
    // Framework Component Control dispatch. Classes used:
    //   class=GlobalTerrainSurface (8), id=RunwayFriction (100)
    //       component_state = 0-15 runway condition index (Dry/Wet/
    //       Standing Water/Snow/Ice/Snow+Ice × 3 severities). Earlier
    //       protocol versions used a user-defined 0xCB packet.
    //   class=System (13), id=SimFreezeState (200)
    //       component_state = 0 (RUNNING) / 1 (FROZEN). Emitted every
    //       frame by the host; drives regen_weather gating.
    void OnCompCtrl(const cigi_session::CompCtrlFields & f) override
    {
        using cigi_session::GlobalTerrainComponentId;
        using cigi_session::SystemComponentId;
        constexpr uint8_t CLASS_GLOBAL_TERRAIN =
            static_cast<uint8_t>(8);   // matches HostSession::ComponentClass::GlobalTerrainSurface
        constexpr uint8_t CLASS_SYSTEM = static_cast<uint8_t>(13);

        if (f.component_class == CLASS_GLOBAL_TERRAIN &&
            f.component_id ==
                static_cast<uint16_t>(GlobalTerrainComponentId::RunwayFriction))
        {
            pending_wx.runway_condition_idx = f.component_state;
            pending_wx.dirty = true;
            g_blend_dirty    = true;
            return;
        }

        if (f.component_class == CLASS_SYSTEM &&
            f.component_id ==
                static_cast<uint16_t>(SystemComponentId::SimFreezeState))
        {
            bool new_frozen = (f.component_state != 0);
            if (new_frozen != g_sim_frozen) {
                g_sim_frozen  = new_frozen;
                g_blend_dirty = true;   // freeze flip can flush a deferred regen
                XPLMDebugString(g_sim_frozen
                    ? "xplanecigi: sim freeze → FROZEN\n"
                    : "xplanecigi: sim freeze → RUNNING\n");
            }
            return;
        }

        char msg[128];
        snprintf(msg, sizeof msg,
            "xplanecigi: Component Control class=%u id=%u state=%u — ignored\n",
            f.component_class, f.component_id, f.component_state);
        XPLMDebugString(msg);
    }
};

static IgProcessor g_ig_proc;

// ─────────────────────────────────────────────────────────────────────────────
// Parse a CIGI datagram (may contain multiple back-to-back packets) via the
// session. Each packet dispatches to a g_ig_proc.OnXxx method. After the
// entire datagram is parsed we emit one SOF to the host (unchanged
// behavior).
// ─────────────────────────────────────────────────────────────────────────────
static void process_datagram(const uint8_t * buf, int len)
{
    g_ig_session.HandleDatagram(buf, static_cast<size_t>(len));
    ++g_ig_frame;
    send_sof_only();
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
// ─────────────────────────────────────────────────────────────────────────────
static XPLMWeatherInfo_t build_weather_info(const PendingPatch & p)
{
    XPLMWeatherInfo_t info = {};
    info.structSize = sizeof info;

    // ── 1. Seed wind layers with "undefined" sentinel ───────────────────────
    for (int i = 0; i < XPLM_NUM_WIND_LAYERS; ++i) {
        info.wind_layers[i].alt_msl   = XPLM_WIND_UNDEFINED_LAYER;
        info.wind_layers[i].speed     = 0.0f;
        info.wind_layers[i].direction = 0.0f;
    }

    // Overlay valid pending_wx wind layers (flag-gated, not index-gated)
    for (int i = 0; i < XPLM_NUM_WIND_LAYERS; ++i) {
        if (!pending_wx.wind[i].valid) continue;
        info.wind_layers[i].alt_msl   = pending_wx.wind[i].alt_m;
        info.wind_layers[i].speed     = pending_wx.wind[i].speed_ms;
        info.wind_layers[i].direction = pending_wx.wind[i].dir_deg;
    }

    // ── 2. Seed cloud layers ────────────────────────────────────────────────
    for (int i = 0; i < XPLM_NUM_CLOUD_LAYERS; ++i) {
        if (!pending_wx.cloud[i].valid) continue;
        info.cloud_layers[i].cloud_type = pending_wx.cloud[i].type_xp;
        info.cloud_layers[i].coverage   = pending_wx.cloud[i].coverage;
        info.cloud_layers[i].alt_base   = pending_wx.cloud[i].base_m;
        info.cloud_layers[i].alt_top    = pending_wx.cloud[i].top_m;
    }

    // ── 3. Scalars with validity clamps ─────────────────────────────────────
    info.temperature_alt =
        (pending_wx.temperature_c > -90.0f && pending_wx.temperature_c < 60.0f)
            ? pending_wx.temperature_c : 15.0f;
    info.visibility =
        (pending_wx.visibility_m > 100.0f)
            ? pending_wx.visibility_m : 10000.0f;
    info.pressure_sl =
        (pending_wx.pressure_hpa > 800.0f && pending_wx.pressure_hpa < 1100.0f)
            ? pending_wx.pressure_hpa * 100.0f : 101325.0f;   // hPa → Pa
    info.pressure_alt = 0.0f;                                 // 0 = use pressure_sl
    info.precip_rate =
        (pending_wx.rain_pct >= 0.0f && pending_wx.rain_pct <= 1.0f)
            ? pending_wx.rain_pct : 0.0f;

    // ── 4. Overlay patch-authored cloud layers ──────────────────────────────
    for (const auto & cl : p.cloud_layers) {
        if (!cl.weather_enable) continue;
        if (cl.layer_id < 1 || cl.layer_id > 3) continue;
        int slot = cl.layer_id - 1;
        info.cloud_layers[slot].cloud_type = remap_cloud_type(cl.cloud_type);
        info.cloud_layers[slot].coverage   = cl.coverage_pct / 100.0f;
        info.cloud_layers[slot].alt_base   = cl.base_elevation_m;
        info.cloud_layers[slot].alt_top    = cl.base_elevation_m + cl.thickness_m;
    }

    // ── 5. Overlay patch-authored wind layers ───────────────────────────────
    for (const auto & wl : p.wind_layers) {
        if (!wl.weather_enable) continue;
        if (wl.layer_id < 10 || wl.layer_id > 12) continue;
        int slot = wl.layer_id - 10;
        info.wind_layers[slot].alt_msl   = wl.altitude_msl_m;
        info.wind_layers[slot].speed     = wl.wind_speed_ms;
        info.wind_layers[slot].direction = wl.wind_direction_deg;
    }

    // ── 6. Overlay scalar overrides with threshold checks ───────────────────
    for (const auto & ov : p.scalar_overrides) {
        if (!ov.weather_enable) continue;
        if (ov.layer_id == 20) {
            if (!std::isnan(ov.visibility_m)  && ov.visibility_m  > 1.0f) {
                info.visibility = ov.visibility_m;
            }
            if (!std::isnan(ov.temperature_c) && ov.temperature_c > -200.0f) {
                info.temperature_alt = ov.temperature_c;
            }
        } else if (ov.layer_id == 21) {
            if (!std::isnan(ov.precipitation_rate)) {
                info.precip_rate = ov.precipitation_rate;
            }
        }
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// Blend-at-ownship — compute effective PendingWeather from the global
// pending_wx and active patches at the ownship lat/lon.
// See DECISIONS.md 2026-04-23 for the rationale (X-Plane SDK regional-
// weather rendering limitation workaround).
//
// Algorithm:
//   For each patch, compute great-circle distance from ownship to patch
//   centre (equirectangular approximation, error < 1% at 50 NM):
//     - distance ≤ corner_radius_m            → weight = 1.0 (Inside)
//     - distance ≤ corner_radius_m + transition_perimeter_m
//           → weight = 1 - (d - corner_radius_m) / transition_perimeter_m
//             (Transition)
//     - else                                  → weight = 0   (Outside)
//   Multi-patch overlap: highest-weight patch wins (deterministic).
//
// Blended fields (lerp by weight): visibility, cloud layers 1-3, precip rate.
// Global-only on the visual path: temperature, pressure, wind, humidity,
// runway condition (X-Plane runway_friction is a single global dataref;
// patch override is handled host-side for the FDM via AtmosphereState).
// ─────────────────────────────────────────────────────────────────────────────

static double approx_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double DEG_TO_RAD = 3.141592653589793 / 180.0;
    constexpr double R_EARTH_M  = 6371000.0;
    const double mean_lat_rad = ((lat1 + lat2) * 0.5) * DEG_TO_RAD;
    const double dlat = (lat2 - lat1) * DEG_TO_RAD;
    const double dlon = (lon2 - lon1) * DEG_TO_RAD * std::cos(mean_lat_rad);
    return std::sqrt(dlat * dlat + dlon * dlon) * R_EARTH_M;
}

// Returns weight in [0, 1] and zone classification for one patch vs ownship.
struct PatchInfluence { float weight; PatchZone zone; };

static PatchInfluence patch_influence(const PendingPatch & p,
                                       double lat, double lon)
{
    const double d = approx_distance_m(p.lat_deg, p.lon_deg, lat, lon);
    if (d <= p.corner_radius_m) {
        return { 1.0f, PatchZone::Inside };
    }
    const double transition_end = p.corner_radius_m + p.transition_perimeter_m;
    if (d <= transition_end && p.transition_perimeter_m > 1e-3) {
        const float w = static_cast<float>(
            1.0 - (d - p.corner_radius_m) / p.transition_perimeter_m);
        return { std::clamp(w, 0.0f, 1.0f), PatchZone::Transition };
    }
    return { 0.0f, PatchZone::Outside };
}

// Cheap zone-state check vs g_patch_last_zone; does not run the full blend.
// Returns true if any active patch has changed inside/transition/outside
// state since the last blend, OR if a known patch is now missing from the
// last-zone map (newly added).
static bool ownship_crossed_zone_boundary()
{
    for (const auto & kv : g_pending_patches) {
        const auto inf = patch_influence(kv.second, g_ownship_lat, g_ownship_lon);
        const auto it  = g_patch_last_zone.find(kv.first);
        if (it == g_patch_last_zone.end()) return true;
        if (it->second != inf.zone)        return true;
    }
    return false;
}

// True if at least one patch currently exerts non-zero influence at ownship.
static bool any_patch_active_at_ownship()
{
    for (const auto & kv : g_pending_patches) {
        if (patch_influence(kv.second, g_ownship_lat, g_ownship_lon).weight > 0.0f)
            return true;
    }
    return false;
}

// Pick the patch (and its weight) with the largest influence at ownship.
// Returns nullptr if no patch is active. Deterministic on overlap because
// std::map iterates in sorted region_id order — equal-weight ties resolve to
// the lowest region_id.
static const PendingPatch * dominant_patch_at_ownship(float & out_weight)
{
    const PendingPatch * best = nullptr;
    float best_w = 0.0f;
    for (const auto & kv : g_pending_patches) {
        const auto inf = patch_influence(kv.second, g_ownship_lat, g_ownship_lon);
        if (inf.weight > best_w) {
            best_w = inf.weight;
            best   = &kv.second;
        }
    }
    out_weight = best_w;
    return best;
}

// Pull a per-patch scalar override layer. Returns nullptr if the patch
// doesn't author it.
static const PendingScalarOverride * find_scalar_override(const PendingPatch & p,
                                                          uint8_t layer_id)
{
    for (const auto & ov : p.scalar_overrides) {
        if (ov.layer_id == layer_id && ov.weather_enable) return &ov;
    }
    return nullptr;
}

// Compute the effective PendingWeather at ownship by lerping the dominant
// patch's overrides into the global state. Updates g_patch_last_zone and
// g_last_blend_{lat,lon,time} as a side effect.
//
// Returns the result by value; the caller is responsible for stamping
// pending_wx and the cloud_changed flag where appropriate.
static PendingWeather blend_weather_at_ownship()
{
    // Refresh per-patch zone tracking unconditionally (needed by
    // ownship_crossed_zone_boundary on the next tick).
    g_patch_last_zone.clear();
    for (const auto & kv : g_pending_patches) {
        g_patch_last_zone[kv.first] =
            patch_influence(kv.second, g_ownship_lat, g_ownship_lon).zone;
    }

    PendingWeather eff = pending_wx;   // start from global

    float w = 0.0f;
    const PendingPatch * patch = dominant_patch_at_ownship(w);
    if (patch == nullptr || w <= 0.0f) {
        // Outside all patches — global state is the answer.
        g_last_blend_lat  = g_ownship_lat;
        g_last_blend_lon  = g_ownship_lon;
        g_last_blend_time = XPLMGetElapsedTime();
        return eff;
    }

    // ── Visibility (scalar override, layer 20) ────────────────────────────
    if (const auto * ov = find_scalar_override(*patch, 20)) {
        // Wire convention: visibility_m == 0 means "not overridden"
        if (ov->visibility_m > 0.0f) {
            eff.visibility_m =
                eff.visibility_m * (1.0f - w) + ov->visibility_m * w;
        }
    }

    // ── Precipitation rate (scalar override, layer 21) ────────────────────
    // Wire-side carries rate as coverage_pct/100 in OnWeatherCtrl, but the
    // current decoder doesn't store it on the patch struct — left as a
    // known limitation; if/when the decoder propagates precip per-patch
    // this branch will start picking it up.
    if (const auto * ov = find_scalar_override(*patch, 21)) {
        if (!std::isnan(ov->precipitation_rate)) {
            const float pct = ov->precipitation_rate * 100.0f;
            const float new_rain =
                eff.rain_pct * (1.0f - w) + pct * w;
            if (new_rain != eff.rain_pct) eff.cloud_changed = true;
            eff.rain_pct = new_rain;
        }
    }

    // ── Cloud layers 1-3 ──────────────────────────────────────────────────
    // Inside the patch (w >= 0.9): use patch layers wholesale.
    // Transition (w in [0.1, 0.9]): keep patch base/top/type, fade coverage
    //   from patch toward global so coverage % moves smoothly while the
    //   silhouette doesn't morph mid-frame.
    // Far-outside (w < 0.1): fall back to global layers; this is the cutover
    //   that prevents lingering wisps of patch cloud type far from the patch.
    constexpr float CLOUD_PATCH_FULL_W = 0.9f;
    constexpr float CLOUD_PATCH_MIN_W  = 0.1f;
    const auto & pcl = patch->cloud_layers;
    for (size_t i = 0; i < 3 && i < pcl.size(); ++i) {
        const auto & src = pcl[i];
        if (!src.weather_enable) continue;
        auto & dst = eff.cloud[i];

        const float patch_type_xp = remap_cloud_type(src.cloud_type);
        const float patch_cov     = src.coverage_pct / 100.0f;
        const float patch_base_m  = src.base_elevation_m;
        const float patch_top_m   = src.base_elevation_m + src.thickness_m;

        bool prev_valid = dst.valid;
        float prev_type = dst.type_xp;
        float prev_cov  = dst.coverage;
        float prev_base = dst.base_m;
        float prev_top  = dst.top_m;

        if (w >= CLOUD_PATCH_FULL_W) {
            dst.type_xp  = patch_type_xp;
            dst.coverage = patch_cov;
            dst.base_m   = patch_base_m;
            dst.top_m    = patch_top_m;
            dst.valid    = true;
        } else if (w >= CLOUD_PATCH_MIN_W) {
            // Coverage fades; base/top/type pinned to patch to avoid morph.
            const float global_cov = eff.cloud[i].valid ? eff.cloud[i].coverage : 0.0f;
            dst.type_xp  = patch_type_xp;
            dst.coverage = global_cov * (1.0f - w) + patch_cov * w;
            dst.base_m   = patch_base_m;
            dst.top_m    = patch_top_m;
            dst.valid    = true;
        }
        // else (w < CLOUD_PATCH_MIN_W): leave eff.cloud[i] as-copied-from-global

        const bool changed = (dst.valid != prev_valid)
            || dst.type_xp  != prev_type
            || dst.coverage != prev_cov
            || dst.base_m   != prev_base
            || dst.top_m    != prev_top;
        if (changed) eff.cloud_changed = true;
    }

    g_last_blend_lat  = g_ownship_lat;
    g_last_blend_lon  = g_ownship_lon;
    g_last_blend_time = XPLMGetElapsedTime();
    return eff;
}

// ─────────────────────────────────────────────────────────────────────────────
// Slice 3c — Erase all regional weather samples from X-Plane and clear local
// tracking state. Called on:
//   - Entry to IG Mode=Reset (host signals reposition or startup reset)
//   - XPluginDisable (plugin unload or X-Plane shutdown)
// ─────────────────────────────────────────────────────────────────────────────
static void cleanup_regional_weather()
{
    if (g_xp_applied.empty()) {
        g_pending_patches.clear();
        return;
    }

    char msg[160];
    snprintf(msg, sizeof msg,
        "xplanecigi: cleanup — erasing %zu applied regions\n",
        g_xp_applied.size());
    XPLMDebugString(msg);

    XPLMBeginWeatherUpdate();
    for (const auto & kv : g_xp_applied) {
        XPLMEraseWeatherAtLocation(kv.second.lat_deg, kv.second.lon_deg);
        snprintf(msg, sizeof msg,
            "xplanecigi: cleanup erased region %u at %.6f,%.6f\n",
            kv.second.region_id, kv.second.lat_deg, kv.second.lon_deg);
        XPLMDebugString(msg);
    }
    XPLMEndWeatherUpdate(/*isIncremental=*/1, /*updateImmediately=*/1);

    g_xp_applied.clear();
    g_pending_patches.clear();

    // Visual refresh — bypasses g_regen_mode gating
    if (cmd_regen_weather) {
        XPLMCommandOnce(cmd_regen_weather);
        XPLMDebugString("xplanecigi: cleanup fired regen_weather\n");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Weather flight loop: write decoded CIGI weather to X-Plane datarefs (~1 Hz)
// ─────────────────────────────────────────────────────────────────────────────
static float WeatherFlightLoopCb(float, float, int, void *)
{
    // ── Blend-at-ownship evaluation (PluginWeatherMode::BlendAtOwnship) ──
    // Computes the effective weather at ownship from global + active patches.
    // pending_wx is the AUTHORED-GLOBAL cache (decoders write here); 'effective'
    // is what actually gets written to X-Plane datarefs this tick. Keeping
    // them separate is what lets the visibility revert to global when the
    // ownship leaves a patch — the blend re-derives effective from the
    // intact global cache instead of the previous (patched) effective.
    PendingWeather effective = pending_wx;

    if (g_plugin_weather_mode == PluginWeatherMode::BlendAtOwnship) {
        const double now = XPLMGetElapsedTime();

        bool trigger = g_blend_dirty;
        if (!trigger && ownship_crossed_zone_boundary()) trigger = true;
        if (!trigger && (now - g_last_blend_time) > BLEND_SMOOTH_RECOMPUTE_S) {
            const double dmoved = approx_distance_m(
                g_last_blend_lat, g_last_blend_lon,
                g_ownship_lat,    g_ownship_lon);
            if (dmoved > BLEND_SMOOTH_MIN_MOVE_M && any_patch_active_at_ownship()) {
                trigger = true;
            }
        }

        if (trigger && (now - g_last_blend_time) >= BLEND_MIN_INTERVAL_S) {
            effective = blend_weather_at_ownship();
            pending_wx.dirty = true;
            if (effective.cloud_changed) pending_wx.cloud_changed = true;
            g_blend_dirty = false;
        }
        // else: g_blend_dirty stays set; next permitted tick retries.
    }

    // Global weather writes — gated on pending_wx.dirty (existing behavior).
    if (pending_wx.dirty) {

    // Apply-now gate — same shape as the regen_weather gate below. Setting
    // sim/weather/region/update_immediately=1 forces X-Plane to rebuild the
    // weather region model in the next frame, which is the source of the
    // multi-second freeze on big deltas (vis 50km → 800m on patch entry,
    // cloud type change, etc.). In flight under ground_only / ground_or_frozen,
    // skip the immediate flag so X-Plane applies the new dataref values on
    // its natural cadence (~5-30s smooth fade) instead of one frozen frame.
    bool apply_now_ok = false;
    switch (g_regen_mode) {
        case RegenMode::Always:         apply_now_ok = true;  break;
        case RegenMode::Never:          apply_now_ok = false; break;
        case RegenMode::GroundOnly:
            apply_now_ok = dr_onground_any
                && XPLMGetDatai(dr_onground_any) != 0;
            break;
        case RegenMode::GroundOrFrozen:
            apply_now_ok = (dr_onground_any && XPLMGetDatai(dr_onground_any) != 0)
                        || g_sim_frozen;
            break;
    }
    if (dr_update_immediately)
        XPLMSetDatai(dr_update_immediately, apply_now_ok ? 1 : 0);

    // Global atmosphere — written from 'effective' so blended values reach
    // X-Plane in BlendAtOwnship mode; in SdkRegional mode effective ==
    // pending_wx so behaviour is unchanged.
    if (dr_sealevel_temp_c)
        XPLMSetDataf(dr_sealevel_temp_c, effective.temperature_c);
    if (dr_sealevel_pressure_pas)
        XPLMSetDataf(dr_sealevel_pressure_pas, effective.pressure_hpa * 100.0f);
    if (dr_visibility_sm)
        XPLMSetDataf(dr_visibility_sm, effective.visibility_m / 1609.34f);
    if (dr_rain_percent)
        XPLMSetDataf(dr_rain_percent, effective.rain_pct);

    // Cloud layers [3]
    float cloud_base[3] = {}, cloud_top[3] = {}, cloud_cov[3] = {}, cloud_tp[3] = {};
    for (int i = 0; i < 3; i++) {
        if (effective.cloud[i].valid) {
            cloud_base[i] = effective.cloud[i].base_m;
            cloud_top[i]  = effective.cloud[i].top_m;
            cloud_cov[i]  = effective.cloud[i].coverage;
            cloud_tp[i]   = effective.cloud[i].type_xp;
        }
    }
    if (dr_cloud_base)     XPLMSetDatavf(dr_cloud_base,     cloud_base, 0, 3);
    if (dr_cloud_tops)     XPLMSetDatavf(dr_cloud_tops,     cloud_top,  0, 3);
    if (dr_cloud_coverage) XPLMSetDatavf(dr_cloud_coverage, cloud_cov,  0, 3);
    if (dr_cloud_type)     XPLMSetDatavf(dr_cloud_type,     cloud_tp,   0, 3);

    // Wind layers [13] — wind is intentionally global-only on the visual
    // path, so reading from pending_wx (== effective in non-blend mode) is
    // correct in both modes.
    struct AuthoredWind {
        float alt_m;
        float dir_deg;
        float spd_kt;
        float turb_0_10;
    };
    std::vector<AuthoredWind> authored;
    authored.reserve(13);
    for (int i = 0; i < 13; i++) {
        if (pending_wx.wind[i].valid) {
            authored.push_back({
                pending_wx.wind[i].alt_m,
                pending_wx.wind[i].dir_deg,
                pending_wx.wind[i].speed_ms / 0.51444f,  // m/s → kts
                pending_wx.wind[i].turb * 10.0f,         // 0-1 → 0-10
            });
        }
    }
    if (!authored.empty()) {
        std::sort(authored.begin(), authored.end(),
                  [](const AuthoredWind & a, const AuthoredWind & b) {
                      return a.alt_m < b.alt_m;
                  });
        float w_alt[13]={}, w_dir[13]={}, w_spd[13]={}, w_turb[13]={};
        const int n = static_cast<int>(authored.size());
        for (int i = 0; i < n; i++) {
            w_alt[i]  = authored[i].alt_m;
            w_dir[i]  = authored[i].dir_deg;
            w_spd[i]  = authored[i].spd_kt;
            w_turb[i] = authored[i].turb_0_10;
        }
        const AuthoredWind & top = authored.back();
        for (int i = n; i < 13; i++) {
            w_alt[i]  = top.alt_m + (i - (n - 1)) * 5000.0f;
            w_dir[i]  = top.dir_deg;
            w_spd[i]  = top.spd_kt;
            w_turb[i] = top.turb_0_10;
        }
        if (dr_wind_alt)  XPLMSetDatavf(dr_wind_alt,  w_alt,  0, 13);
        if (dr_wind_dir)  XPLMSetDatavf(dr_wind_dir,  w_dir,  0, 13);
        if (dr_wind_spd)  XPLMSetDatavf(dr_wind_spd,  w_spd,  0, 13);
        if (dr_wind_turb) XPLMSetDatavf(dr_wind_turb, w_turb, 0, 13);
    }

    // Runway friction — direct passthrough (XP enum matches our 0-15).
    // Not blended: X-Plane's runway_friction dataref is a single global
    // scalar, and the host already resolves patch overrides on the FDM
    // path via AtmosphereState.
    if (dr_runway_friction)
        XPLMSetDataf(dr_runway_friction, static_cast<float>(pending_wx.runway_condition_idx));

    // Always flip back to 0 after the writes, regardless of apply_now_ok —
    // this is the "I'm done writing for now" handshake. Idempotent.
    if (dr_update_immediately)
        XPLMSetDatai(dr_update_immediately, 0);

    // Regen_weather firing rule (per DECISIONS.md 2026-04-23 design). Fires
    // only when cloud_changed AND gate satisfied AND ≥REGEN_DEBOUNCE_S since
    // last regen. When the gate isn't satisfied (e.g. mid-flight under
    // ground_or_frozen), cloud_changed is KEPT — deferred until next ground
    // touch or freeze. The debounce only guards against a gate-flip double
    // fire when multiple mutations accumulated before gate opened.
    if (pending_wx.cloud_changed && cmd_regen_weather) {
        bool gate_ok = false;
        switch (g_regen_mode) {
            case RegenMode::Always:         gate_ok = true;  break;
            case RegenMode::Never:          gate_ok = false; break;
            case RegenMode::GroundOnly:
                gate_ok = dr_onground_any
                    && XPLMGetDatai(dr_onground_any) != 0;
                break;
            case RegenMode::GroundOrFrozen:
                gate_ok = (dr_onground_any && XPLMGetDatai(dr_onground_any) != 0)
                       || g_sim_frozen;
                break;
        }
        const double now = XPLMGetElapsedTime();
        const bool debounced = (now - g_last_regen_time) >= REGEN_DEBOUNCE_S;
        if (gate_ok && debounced) {
            XPLMCommandOnce(cmd_regen_weather);
            g_last_regen_time = now;
            pending_wx.cloud_changed = false;   // fired; deferral cleared
            XPLMDebugString("xplanecigi: regen_weather fired\n");
        }
        // else: keep cloud_changed=true; retried on the next tick that
        // satisfies the gate.
    }

    // Log once on change — values reflect what was actually written to
    // X-Plane (effective), so the log shows blended values inside a patch
    // instead of the global cache.
    char dbg[256];
    snprintf(dbg, sizeof(dbg),
        "xplanecigi: WX applied — vis=%.0fm temp=%.1fC wind=%03.0f/%.1fms clouds=%d rain=%.0f%% rwy_fric=%u\n",
        effective.visibility_m, effective.temperature_c,
        pending_wx.wind_dir_deg, pending_wx.wind_speed_ms,
        (int)(effective.cloud[0].valid + effective.cloud[1].valid + effective.cloud[2].valid),
        effective.rain_pct * 100.0f,
        (unsigned)pending_wx.runway_condition_idx);
    XPLMDebugString(dbg);

    pending_wx.dirty = false;
    }  // end if (pending_wx.dirty)

    // ── Slice 3b: Regional weather patch apply / erase ──────────────────────
    // Only runs in PluginWeatherMode::SdkRegional. In BlendAtOwnship mode the
    // patches are consumed by blend_weather_at_ownship above; no per-patch
    // XPLMSetWeatherAtLocation calls are made.
    if (g_plugin_weather_mode == PluginWeatherMode::SdkRegional
        && (!g_pending_patches.empty() || !g_xp_applied.empty())) {
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
    if (g_probing_terrain && g_terrain_probe) {
        double now = XPLMGetElapsedTime();
        if (now - g_last_probe_t >= PROBE_INTERVAL_S) {
            g_last_probe_t = now;

            double lx, ly, lz;
            XPLMWorldToLocal(g_ownship_lat, g_ownship_lon, 0.0, &lx, &ly, &lz);
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
    strcpy(outDesc, "CIGI 3.3 IG plugin — driven by sim_cigi_bridge (ROS2)");

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
    g_surface_type = XPLMFindDataRef("sim/flightmodel/ground/surface_texture_type");
    if (!g_surface_type) {
        g_surface_type = XPLMFindDataRef("sim/flightmodel2/ground/surface_texture_type");
    }
    XPLMDebugString(g_surface_type
        ? "xplanecigi: surface_type dataref bound\n"
        : "xplanecigi: WARNING — no surface_type dataref found, HAT surface=UNKNOWN\n");

    // Ownship altitude MSL (metres)
    g_elevation = XPLMFindDataRef("sim/flightmodel/position/elevation");

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

    // ── Wire inbound processors ──────────────────────────────────────────
    g_ig_session.SetIgCtrlProcessor     (&g_ig_proc);
    g_ig_session.SetEntityCtrlProcessor (&g_ig_proc);
    g_ig_session.SetHatHotReqProcessor  (&g_ig_proc);
    g_ig_session.SetAtmosphereProcessor (&g_ig_proc);
    g_ig_session.SetEnvRegionProcessor  (&g_ig_proc);
    g_ig_session.SetWeatherCtrlProcessor(&g_ig_proc);
    g_ig_session.SetCompCtrlProcessor   (&g_ig_proc);

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
PLUGIN_API void XPluginDisable()
{
    // Slice 3c: erase all applied regional weather so it doesn't persist
    // into the next plugin-enable session or into X-Plane shutdown.
    cleanup_regional_weather();
}
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void *) {}
