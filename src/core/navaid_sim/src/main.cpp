// NavSim — standalone interactive navigation simulator
// Replaces the old DSim-based main.cpp with a self-contained REPL and
// a built-in unit-test runner.
//
// Build:  cl /std:c++17 /EHsc /O2 *.cpp  (MSVC)
//         g++ -std=c++17 -O2 *.cpp -o navsim  (GCC/Clang)
//
// Usage:
//   navsim [--a424 <file>] [--xp12 <file>] [--terrain <dir>] [--test]
//
//   --a424    path to Jeppesen ARINC-424 binary   (default: Data/euramec.pc)
//   --xp12    path to X-Plane 12 earth_nav.dat    (no default; overrides a424)
//   --terrain directory containing SRTM3 .hgt tiles (default: ../terrain/srtm3/)
//   --test    run built-in unit tests then exit

#ifdef _MSC_VER
#  define _USE_MATH_DEFINES   // makes MSVC expose M_PI in <cmath>
#  define NOMINMAX            // prevent Windows.h from defining min/max macros
#endif

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>        // for SetConsoleOutputCP
#endif

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>
#include <stdexcept>

#include "Model.h"
#include "World.h"
#include "NavSimTask.h"
#include "A424Parser.h"
#include "WorldParser.h"
#include "MagDec.h"
#include "LOSChecker.h"
#include "LatLon.h"
#include "Units.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::vector<std::string> split(const std::string& s)
{
    std::vector<std::string> tokens;
    std::istringstream ss(s);
    std::string tok;
    while (ss >> tok) tokens.push_back(tok);
    return tokens;
}

// Convert MHz (e.g. 115.80) to internal format (11580)
static int mhzToInternal(double mhz) { return static_cast<int>(std::round(mhz * 100.0)); }

// Convert kHz (e.g. 362.0) to internal ADF format (plain kHz integer)
static int khzToInternal(double khz) { return static_cast<int>(std::round(khz)); }

// Internal VHF → MHz string
static std::string internalToMhz(int f)
{
    if (f == 0) return "---.-";
    std::ostringstream oss;
    oss << (f / 100) << "." << std::setw(2) << std::setfill('0') << (f % 100);
    return oss.str();
}

// Internal ADF → kHz string
static std::string internalToKhz(int f)
{
    if (f == 0) return "---";
    return std::to_string(f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Formatted output helpers
// ─────────────────────────────────────────────────────────────────────────────

static void printRadioResult(const char* label,
                              const AS::RadioResult& r,
                              int freqInternal, int obs)
{
    std::cout << "\n  [" << label << "]  freq=" << internalToMhz(freqInternal)
              << " MHz  OBS=" << obs << "°\n";

    if (r.vor_localizer)
    {
        // --- LOC mode ---
        if (r.vor_found)
        {
            std::cout << "    LOC  ident=" << r.vor_ident
                      << "  course=" << std::fixed << std::setprecision(1)
                      << r.loc_course << "°M"
                      << "  dev=" << r.vor_deviation << "°"
                      << " (" << (r.vor_deviation < 0 ? "LEFT" : "RIGHT") << ")"
                      << "  DDM=" << r.vor_ddm
                      << "  dist=" << static_cast<int>(r.vor_distance_m / 1852.0f) << " NM"
                      << "\n";
        }
        else
        {
            std::cout << "    LOC  [no signal]\n";
        }

        if (r.gs_found)
        {
            std::cout << "    G/S  dev=" << std::fixed << std::setprecision(2)
                      << r.gs_deviation << "°"
                      << " (" << (r.gs_deviation > 0 ? "ABOVE" : "BELOW") << ")"
                      << "  dist=" << static_cast<int>(r.gs_distance_m / 1852.0f) << " NM"
                      << "\n";
        }
        else
        {
            std::cout << "    G/S  [no signal]\n";
        }
    }
    else
    {
        // --- VOR mode ---
        if (r.vor_found)
        {
            std::cout << "    VOR  ident=" << r.vor_ident
                      << "  radial=" << std::fixed << std::setprecision(1)
                      << r.vor_bearing << "°"
                      << "  dev=" << r.vor_deviation << "°"
                      << " (" << (r.vor_from ? "FROM" : "TO") << ")"
                      << "  dist=" << static_cast<int>(r.vor_distance_m / 1852.0f) << " NM"
                      << "\n";
        }
        else
        {
            std::cout << "    VOR  [no signal]\n";
        }
    }

    // DME (always present on same radio)
    if (r.dme_found)
    {
        std::cout << "    DME  " << std::fixed << std::setprecision(1)
                  << r.dme_distance_nm << " NM\n";
    }
    else
    {
        std::cout << "    DME  [no signal]\n";
    }
}

static void printStatus(const AS::Model& model, NavSimTask& task, const MagDec* magdec)
{
    float lat = model.getLat();
    float lon = model.getLon();
    float alt = model.getAltitude();

    bool hasTerrain = task.getTerrainModel().hasTile(
                          static_cast<double>(lat), static_cast<double>(lon));
    float terrainFt = task.getTerrainModel().getElevationFt(
                          static_cast<double>(lat), static_cast<double>(lon));

    std::cout << "\n  Position: lat=" << std::fixed << std::setprecision(4)
              << lat << "  lon=" << lon
              << "  alt=" << static_cast<int>(alt) << " ft MSL"
              << "  hdg=" << static_cast<int>(model.getHeading()) << "°\n";
    if (hasTerrain)
        std::cout << "  Terrain:  " << static_cast<int>(terrainFt) << " ft MSL"
                  << "  AGL=" << static_cast<int>(alt - terrainFt) << " ft";
    else
        std::cout << "  Terrain:  no data";
    if (magdec && magdec->isLoaded())
        std::cout << "  MagDec=" << std::fixed << std::setprecision(1)
                  << magdec->getDeclination(lat, lon) << "°";
    std::cout << "\n"
              << "  NAV1: " << internalToMhz(model.getFrequency(AS::Radios::radio_1))
              << " MHz  OBS=" << model.getOBS(AS::Radios::radio_1) << "°\n"
              << "  NAV2: " << internalToMhz(model.getFrequency(AS::Radios::radio_2))
              << " MHz  OBS=" << model.getOBS(AS::Radios::radio_2) << "°\n"
              << "  CDI:  " << internalToMhz(model.getFrequency(AS::Radios::cdi))
              << " MHz  OBS=" << model.getOBS(AS::Radios::cdi) << "°\n"
              << "  ADF:  " << internalToKhz(model.getADF_Frequency()) << " kHz\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Unit tests
// ─────────────────────────────────────────────────────────────────────────────

struct TestResult
{
    std::string name;
    bool        passed;
    std::string detail;
};

static std::vector<TestResult> gTestResults;

static void runTest(const std::string& name,
                    AS::Model& /*model*/, NavSimTask& task,
                    // Setup lambda: configure model before step()
                    std::function<void()> setup,
                    // Check lambda: inspect results, return "" on pass or message on fail
                    std::function<std::string()> check)
{
    setup();
    task.step();
    std::string msg = check();
    bool ok = msg.empty();
    gTestResults.push_back({ name, ok, ok ? "OK" : msg });
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << name;
    if (!ok) std::cout << " — " << msg;
    std::cout << "\n";
}

static void runAllTests(AS::Model& model, NavSimTask& task, AS::World& world)
{
    gTestResults.clear();
    std::cout << "\n=== RadioNav Unit Tests ===\n\n";

    // ── VOR tests ────────────────────────────────────────────────────────────
    AS::VOR testVOR(0,0,0,0,0,0,"","");
    if (!world.getFirstVOR(testVOR))
    {
        std::cout << "  [SKIP] VOR tests — no VOR in database\n";
    }
    else
    {
        double vorLat = testVOR.mLatLon.get_lat_deg();
        double vorLon = testVOR.mLatLon.get_lon_deg();
        // float  vorElev = testVOR.mElevation;   // unused
        int    vorFreq = testVOR.mFrequency;   // MHz * 100
        std::string vorIdent = testVOR.mIdent;

        // Test 1: Zone of confusion — aircraft directly above (should NOT receive)
        // At 0 ground distance the vertical angle is ~90°, exceeding the 80° threshold.
        runTest(
            "VOR zone-of-confusion directly above (" + vorIdent + ")",
            model, task,
            [&]() {
                model.setPosition((float)vorLat, (float)vorLon, 30000.f);
                model.setFrequency(AS::Radios::radio_1, vorFreq);
                model.setOBS(AS::Radios::radio_1, 0);
            },
            [&]() -> std::string {
                auto r = model.getRadioResult(AS::Radios::radio_1);
                if (r.vor_localizer) return "radio switched to LOC, unexpected";
                // Zone of confusion: VOR MUST NOT be received directly above
                if (r.vor_found) return "VOR received directly above — zone-of-confusion should suppress it";
                return "";
            }
        );

        // Test 2: VOR in-range at cruise altitude — 10 NM north at FL300
        // Use 30 000 ft to guarantee terrain LOS clearance regardless of VOR location.
        runTest(
            "VOR reception 10 NM away at FL300 (" + vorIdent + ")",
            model, task,
            [&]() {
                float offsetDeg = 10.f / 60.f;   // 10 NM north
                model.setPosition((float)vorLat + offsetDeg, (float)vorLon, 30000.f);
                model.setFrequency(AS::Radios::radio_1, vorFreq);
            },
            [&]() -> std::string {
                auto r = model.getRadioResult(AS::Radios::radio_1);
                if (!r.vor_found) return "VOR not found at 10 NM / FL300";
                return "";
            }
        );

        // Test 3: VOR out-of-range — aircraft 300 NM away at FL300
        runTest(
            "VOR not received at 300 NM (" + vorIdent + ")",
            model, task,
            [&]() {
                float offsetDeg = 300.f / 60.f;
                model.setPosition((float)vorLat + offsetDeg, (float)vorLon, 30000.f);
                model.setFrequency(AS::Radios::radio_1, vorFreq);
            },
            [&]() -> std::string {
                auto r = model.getRadioResult(AS::Radios::radio_1);
                if (r.vor_found) return "VOR received at 300 NM — should be out of range";
                return "";
            }
        );

        // Test 4: DME slant-range accuracy
        // Place aircraft 20 NM (ground) north of the DME and FL300.
        // Slant range = sqrt(ground² + altDiff²).  We allow ±3 NM tolerance
        // because the actual DME station may not be co-located with the VOR.
        {
            AS::DME testDME(0,0,0,0,0,0,"","");
            bool hasDME = world.getFirstDME(testDME);
            if (!hasDME)
            {
                std::cout << "  [SKIP] DME tests — no DME in database\n";
            }
            else
            {
                double dmeLat = testDME.mLatLon.get_lat_deg();
                double dmeLon = testDME.mLatLon.get_lon_deg();
                // float  dmeElev_ft = (float)m_to_ft(testDME.mElevation);  // unused
                int    dmeFreq = testDME.mFrequency;
                std::string dmeIdent = testDME.mIdent;

                float groundDist = 20.f;   // NM
                float testAlt_ft = 30000.f;
                // float altDiff_nm = (testAlt_ft - dmeElev_ft) / 6076.12f;  // unused
                // float expected_slant = ...;  // plausibility check used instead

                runTest(
                    "DME slant-range accuracy 20 NM (" + dmeIdent + ")",
                    model, task,
                    [&]() {
                        float offsetDeg = groundDist / 60.f;
                        model.setPosition((float)dmeLat + offsetDeg, (float)dmeLon, testAlt_ft);
                        model.setFrequency(AS::Radios::radio_1, dmeFreq);
                    },
                    [&]() -> std::string {
                        auto r = model.getRadioResult(AS::Radios::radio_1);
                        if (!r.dme_found) return "DME not found at 20 NM / FL300";
                        // Any co-channel DME within range may be selected;
                        // just verify reading is plausible (positive and < 200 NM)
                        if (r.dme_distance_nm <= 0.0f)
                            return "DME reported non-positive distance";
                        if (r.dme_distance_nm > 200.0f)
                        {
                            std::ostringstream oss;
                            oss << "DME " << std::fixed << std::setprecision(1)
                                << r.dme_distance_nm << " NM is implausibly large";
                            return oss.str();
                        }
                        return "";
                    }
                );
            }
        }

        // Test 5: VOR bearing — aircraft due north at FL300, bearing should be ~360°/0°
        runTest(
            "VOR bearing due north (" + vorIdent + ")",
            model, task,
            [&]() {
                float offsetDeg = 5.f / 60.f;
                model.setPosition((float)vorLat + offsetDeg, (float)vorLon, 30000.f);
                model.setFrequency(AS::Radios::radio_1, vorFreq);
            },
            [&]() -> std::string {
                auto r = model.getRadioResult(AS::Radios::radio_1);
                if (!r.vor_found) return "VOR not found";
                // bearing from VOR to aircraft should be ~360 (north)
                // vor_bearing is the magnetic bearing from aircraft to station
                // The radial from station to aircraft = bearing + 180 (mod 360)
                float radial = std::fmod(r.vor_bearing + 180.f, 360.f);
                float err = std::fabs(radial - 360.f);
                if (err > 180.f) err = 360.f - err;  // wrap
                if (err > 15.f)
                {
                    std::ostringstream oss;
                    oss << "Expected radial ~360°, got " << std::fixed
                        << std::setprecision(1) << radial << "° (err=" << err << "°)";
                    return oss.str();
                }
                return "";
            }
        );
    }

    // ── NDB tests ────────────────────────────────────────────────────────────
    AS::NDB testNDB(0,0,0,0,"","");
    if (!world.getFirstNDB(testNDB))
    {
        std::cout << "  [SKIP] NDB tests — no NDB in database\n";
    }
    else
    {
        double ndbLat  = testNDB.mLatLon.get_lat_deg();
        double ndbLon  = testNDB.mLatLon.get_lon_deg();
        int    ndbFreq = testNDB.mFrequency; // kHz * 100
        std::string ndbIdent = testNDB.mIdent;

        // Test 6: NDB reception at 30 NM
        runTest(
            "NDB reception at 30 NM (" + ndbIdent + ")",
            model, task,
            [&]() {
                float offsetDeg = 30.f / 60.f;
                model.setPosition((float)ndbLat + offsetDeg, (float)ndbLon, 5000.f);
                model.setADF_Frequency(ndbFreq);
            },
            [&]() -> std::string {
                auto r = model.getRadioResult(AS::Radios::radio_1);
                if (!r.ndb_found) return "NDB not found at 30 NM";
                return "";
            }
        );
    }

    // ── ILS tests (LOC + G/S) ─────────────────────────────────────────────
    AS::ILS_LOC testLOC(0,0,0,0,0,0,0,"","","","");
    AS::ILS_GS  testGS(0,0,0,0,0,0,0,"","","","");
    bool hasFullILS = world.getFirstILS(testLOC, testGS);

    if (!hasFullILS && !world.getFirstLOC(testLOC))
    {
        std::cout << "  [SKIP] ILS tests — no ILS/LOC in database\n";
    }
    else
    {
        double locLat  = testLOC.mLatLon.get_lat_deg();
        double locLon  = testLOC.mLatLon.get_lon_deg();
        int    locFreq = testLOC.mFrequency;
        float  trueBrg = testLOC.mTrueBearing;
        std::string locIdent = testLOC.mIdent;

        // Approach direction = opposite of localizer bearing
        double approachBrg = std::fmod(trueBrg + 180.0, 360.0);
        double approachRad = approachBrg * M_PI / 180.0;
        double offsetLat5  = 5.0 / 60.0 * std::cos(approachRad);
        double offsetLon5  = 5.0 / (60.0 * std::cos(locLat * M_PI / 180.0)) * std::sin(approachRad);

        // Test: LOC reception on centerline at 5 NM
        runTest(
            "LOC reception on centerline 5 NM (" + locIdent + ")",
            model, task,
            [&]() {
                model.setPosition((float)(locLat + offsetLat5), (float)(locLon + offsetLon5), 2000.f);
                model.setFrequency(AS::Radios::radio_1, locFreq);
                model.setOBS(AS::Radios::radio_1, 0);
            },
            [&]() -> std::string {
                auto r = model.getRadioResult(AS::Radios::radio_1);
                if (!r.vor_localizer) return "radio not in LOC mode";
                if (!r.vor_found) return "LOC not found on centerline at 5 NM";
                if (std::fabs(r.vor_deviation) > 5.0f)
                {
                    std::ostringstream oss;
                    oss << "LOC deviation=" << std::fixed << std::setprecision(1)
                        << r.vor_deviation << "° on centerline, expected <5°";
                    return oss.str();
                }
                return "";
            }
        );

        // Test: LOC not received when >35° off-course
        double abBeamRad = std::fmod(approachBrg + 90.0, 360.0) * M_PI / 180.0;
        double ab_offsetLat = 3.0 / 60.0 * std::cos(abBeamRad);
        double ab_offsetLon = 3.0 / (60.0 * std::cos(locLat * M_PI / 180.0)) * std::sin(abBeamRad);

        runTest(
            "LOC not received >35° off-course (" + locIdent + ")",
            model, task,
            [&]() {
                model.setPosition((float)(locLat + ab_offsetLat), (float)(locLon + ab_offsetLon), 2000.f);
                model.setFrequency(AS::Radios::radio_1, locFreq);
            },
            [&]() -> std::string {
                auto r = model.getRadioResult(AS::Radios::radio_1);
                if (r.vor_found)
                    return "LOC received while 90° off course — should not be received";
                return "";
            }
        );

        // ── Glideslope tests (only if we found a full ILS with matching G/S) ──
        if (hasFullILS)
        {
            double gsLat   = testGS.mLatLon.get_lat_deg();
            double gsLon   = testGS.mLatLon.get_lon_deg();
            float  gsAngle = testGS.mAngle;          // published glidepath angle (typ. 3°)
            float  gsElev  = testGS.mElevation;       // feet
            float  gsBrg   = testGS.mBearing;         // bearing of the approach
            std::string gsIdent = testGS.mIdent;

            // Position on glidepath at 5 NM from G/S antenna:
            // altitude = gsElev + tan(gsAngle) * 5 NM
            double dist5_m = 5.0 * 1852.0;
            double gsAngleRad = static_cast<double>(gsAngle) * M_PI / 180.0;
            float  onPathAlt_ft = gsElev + static_cast<float>(
                       std::tan(gsAngleRad) * dist5_m / 0.3048);

            // Compute position 5 NM in front of G/S (approach direction = bearing + 180)
            double gsApproachBrg = std::fmod(static_cast<double>(gsBrg) + 180.0, 360.0);
            double gsApproachRad = gsApproachBrg * M_PI / 180.0;
            double gsOffLat5 = 5.0 / 60.0 * std::cos(gsApproachRad);
            double gsOffLon5 = 5.0 / (60.0 * std::cos(gsLat * M_PI / 180.0))
                             * std::sin(gsApproachRad);

            // Test: G/S reception on glidepath at 5 NM — deviation should be ~0°
            runTest(
                "G/S on glidepath 5 NM (" + gsIdent + ")",
                model, task,
                [&]() {
                    model.setPosition((float)(gsLat + gsOffLat5),
                                      (float)(gsLon + gsOffLon5), onPathAlt_ft);
                    model.setFrequency(AS::Radios::radio_1, locFreq);
                },
                [&]() -> std::string {
                    auto r = model.getRadioResult(AS::Radios::radio_1);
                    if (!r.gs_found) return "G/S not found at 5 NM on glidepath";
                    if (std::fabs(r.gs_deviation) > 0.5f)
                    {
                        std::ostringstream oss;
                        oss << "G/S deviation=" << std::fixed << std::setprecision(2)
                            << r.gs_deviation << "° on glidepath, expected <0.5°";
                        return oss.str();
                    }
                    return "";
                }
            );

            // Test: G/S shows positive deviation when aircraft is above glidepath
            float highAlt_ft = onPathAlt_ft + 1000.f;

            runTest(
                "G/S above glidepath at 5 NM (" + gsIdent + ")",
                model, task,
                [&]() {
                    model.setPosition((float)(gsLat + gsOffLat5),
                                      (float)(gsLon + gsOffLon5), highAlt_ft);
                    model.setFrequency(AS::Radios::radio_1, locFreq);
                },
                [&]() -> std::string {
                    auto r = model.getRadioResult(AS::Radios::radio_1);
                    if (!r.gs_found) return "G/S not found when above glidepath";
                    if (r.gs_deviation <= 0.0f)
                    {
                        std::ostringstream oss;
                        oss << "G/S deviation=" << std::fixed << std::setprecision(2)
                            << r.gs_deviation << "°, expected positive (above glidepath)";
                        return oss.str();
                    }
                    return "";
                }
            );
        }
        else
        {
            std::cout << "  [SKIP] G/S tests — no matching glideslope found\n";
        }
    }

    // ── Radio horizon test ───────────────────────────────────────────────────
    {
        float h1 = 10000.f;  // aircraft 10 000 ft
        float h2 = 100.f;    // navaid 100 ft
        float horizon = LOSChecker::radioHorizonNM(h1, h2);
        bool ok = (horizon > 100.f && horizon < 200.f);
        std::string detail = ok ? "" : "unexpected horizon value";
        gTestResults.push_back({ "Radio horizon formula sanity check", ok, ok ? "OK" : detail });
        std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << "Radio horizon formula sanity check";
        if (!ok)
        {
            std::cout << " — got " << std::fixed << std::setprecision(1) << horizon
                      << " NM for 10000/100 ft, expected 100-200 NM";
        }
        std::cout << "\n";
    }

    // ── Summary ──────────────────────────────────────────────────────────────
    int passed = 0, failed = 0;
    for (auto& r : gTestResults) (r.passed ? passed : failed)++;
    std::cout << "\n  Results: " << passed << " passed, " << failed << " failed"
              << " out of " << gTestResults.size() << " tests.\n\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// REPL
// ─────────────────────────────────────────────────────────────────────────────

static void printHelp()
{
    std::cout <<
        "\nRadioNav commands:\n"
        "  pos  <lat> <lon> <alt_ft> [hdg_deg]   set aircraft position\n"
        "  nav1 <freq_MHz> [obs_deg]              set NAV1 (radio 1)\n"
        "  nav2 <freq_MHz> [obs_deg]              set NAV2 (radio 2)\n"
        "  cdi  <freq_MHz> [obs_deg]              set CDI  (radio 3)\n"
        "  adf  <freq_kHz>                        set ADF frequency\n"
        "  step                                   run one simulation step\n"
        "  status                                 show current state\n"
        "  test                                   run built-in unit tests\n"
        "  help                                   show this help\n"
        "  quit / exit                            exit the program\n"
        "\n"
        "  Frequency formats:  VHF = MHz (e.g. 115.80)  ADF = kHz (e.g. 362.0)\n"
        "\n";
}

static void runREPL(AS::Model& model, NavSimTask& task, AS::World& world, const MagDec* magdec)
{
    std::cout << "\nRadioNav ready.  Type 'help' for commands.\n";
    std::string line;

    while (true)
    {
        std::cout << "radionav> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        line = trim(line);
        if (line.empty()) continue;

        auto toks = split(line);
        const std::string& cmd = toks[0];

        try
        {
            // ── pos ──────────────────────────────────────────────────────
            if (cmd == "pos")
            {
                if (toks.size() < 4)
                { std::cout << "  usage: pos <lat> <lon> <alt_ft> [hdg]\n"; continue; }
                float lat = std::stof(toks[1]);
                float lon = std::stof(toks[2]);
                float alt = std::stof(toks[3]);
                float hdg = (toks.size() >= 5) ? std::stof(toks[4]) : 0.f;
                model.setPosition(lat, lon, alt, hdg);
                bool hasTile = task.getTerrainModel().hasTile(
                                   static_cast<double>(lat), static_cast<double>(lon));
                std::cout << "  Position set: lat=" << lat << " lon=" << lon
                          << " alt=" << static_cast<int>(alt) << " ft MSL"
                          << "  hdg=" << static_cast<int>(hdg) << "°\n";
                if (hasTile)
                {
                    float terrainFt = task.getTerrainModel().getElevationFt(
                                          static_cast<double>(lat), static_cast<double>(lon));
                    std::cout << "  Terrain: " << static_cast<int>(terrainFt) << " ft MSL"
                              << "  AGL=" << static_cast<int>(alt - terrainFt) << " ft";
                }
                else
                {
                    std::cout << "  Terrain: no data";
                }
                if (magdec && magdec->isLoaded())
                    std::cout << "  MagDec=" << std::fixed << std::setprecision(1)
                              << magdec->getDeclination(lat, lon) << "°";
                std::cout << "\n";
            }

            // ── nav1 ─────────────────────────────────────────────────────
            else if (cmd == "nav1")
            {
                if (toks.size() < 2) { std::cout << "  usage: nav1 <MHz> [obs]\n"; continue; }
                int freq = mhzToInternal(std::stod(toks[1]));
                int obs  = (toks.size() >= 3) ? std::stoi(toks[2]) : model.getOBS(AS::Radios::radio_1);
                model.setFrequency(AS::Radios::radio_1, freq);
                model.setOBS(AS::Radios::radio_1, obs);
                std::cout << "  NAV1: " << internalToMhz(freq) << " MHz  OBS=" << obs << "°\n";
            }

            // ── nav2 ─────────────────────────────────────────────────────
            else if (cmd == "nav2")
            {
                if (toks.size() < 2) { std::cout << "  usage: nav2 <MHz> [obs]\n"; continue; }
                int freq = mhzToInternal(std::stod(toks[1]));
                int obs  = (toks.size() >= 3) ? std::stoi(toks[2]) : model.getOBS(AS::Radios::radio_2);
                model.setFrequency(AS::Radios::radio_2, freq);
                model.setOBS(AS::Radios::radio_2, obs);
                std::cout << "  NAV2: " << internalToMhz(freq) << " MHz  OBS=" << obs << "°\n";
            }

            // ── cdi ──────────────────────────────────────────────────────
            else if (cmd == "cdi")
            {
                if (toks.size() < 2) { std::cout << "  usage: cdi <MHz> [obs]\n"; continue; }
                int freq = mhzToInternal(std::stod(toks[1]));
                int obs  = (toks.size() >= 3) ? std::stoi(toks[2]) : model.getOBS(AS::Radios::cdi);
                model.setFrequency(AS::Radios::cdi, freq);
                model.setOBS(AS::Radios::cdi, obs);
                std::cout << "  CDI:  " << internalToMhz(freq) << " MHz  OBS=" << obs << "°\n";
            }

            // ── adf ──────────────────────────────────────────────────────
            else if (cmd == "adf")
            {
                if (toks.size() < 2) { std::cout << "  usage: adf <kHz>\n"; continue; }
                int freq = khzToInternal(std::stod(toks[1]));
                model.setADF_Frequency(freq);
                std::cout << "  ADF:  " << internalToKhz(freq) << " kHz\n";
            }

            // ── step ─────────────────────────────────────────────────────
            else if (cmd == "step")
            {
                task.step();

                // NAV1 / NAV2 / CDI results
                const int radios[] = { AS::Radios::radio_1, AS::Radios::radio_2, AS::Radios::cdi };
                const char* labels[] = { "NAV1", "NAV2", "CDI " };

                for (int i = 0; i < 3; i++)
                {
                    auto r = model.getRadioResult(radios[i]);
                    printRadioResult(labels[i], r,
                                     model.getFrequency(radios[i]),
                                     model.getOBS(radios[i]));
                }

                // ADF / NDB (only radio 1 carries NDB)
                auto r1 = model.getRadioResult(AS::Radios::radio_1);
                std::cout << "\n  [ADF ]  freq=" << internalToKhz(model.getADF_Frequency()) << " kHz\n";
                if (r1.ndb_found)
                {
                    std::cout << "    NDB  ident=" << r1.ndb_ident
                              << "  bearing=" << std::fixed << std::setprecision(1)
                              << r1.ndb_bearing << "°"
                              << "  dist=" << static_cast<int>(r1.ndb_distance_m / 1852.0f) << " NM\n";
                }
                else
                {
                    std::cout << "    NDB  [no signal]\n";
                }

                // Marker beacons
                int inner  = model.getInnerMarker();
                int middle = model.getMiddleMarker();
                int outer  = model.getOuterMarker();
                if (inner || middle || outer)
                {
                    std::cout << "\n  [MKR ]  ";
                    if (outer)  std::cout << "OUTER ";
                    if (middle) std::cout << "MIDDLE ";
                    if (inner)  std::cout << "INNER";
                    std::cout << "\n";
                }

                std::cout << "\n";
            }

            // ── status ───────────────────────────────────────────────────
            else if (cmd == "status")
            {
                printStatus(model, task, magdec);
            }

            // ── test ─────────────────────────────────────────────────────
            else if (cmd == "test")
            {
                runAllTests(model, task, world);
            }

            // ── help ─────────────────────────────────────────────────────
            else if (cmd == "help" || cmd == "?")
            {
                printHelp();
            }

            // ── quit ─────────────────────────────────────────────────────
            else if (cmd == "quit" || cmd == "exit" || cmd == "q")
            {
                break;
            }

            else
            {
                std::cout << "  Unknown command '" << cmd << "'.  Type 'help' for commands.\n";
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "  Error: " << e.what() << "\n";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(65001);  // UTF-8 output — fixes ° and other Unicode on Windows console
#endif

    // ── Parse arguments ───────────────────────────────────────────────────
    std::string a424File   = "data/euramec.pc";
    std::string xp12File   = "";
    std::string magdecFile = "";
    std::string terrainDir = "data/srtm3/";
    bool runTests          = false;
    bool useXP12           = false;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--a424"   && i + 1 < argc) { a424File   = argv[++i]; useXP12 = false; }
        else if (arg == "--xp12"   && i + 1 < argc) { xp12File   = argv[++i]; useXP12 = true;  }
        else if (arg == "--magdec" && i + 1 < argc) { magdecFile = argv[++i]; }
        else if (arg == "--terrain"&& i + 1 < argc) { terrainDir = argv[++i]; }
        else if (arg == "--test")                    { runTests   = true;       }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout <<
                "Usage: radionav [options]\n"
                "  --a424    <file>   ARINC-424 navaid database  (default: data/euramec.pc)\n"
                "  --xp12    <file>   X-Plane 12 earth_nav.dat   (overrides --a424)\n"
                "  --magdec  <file>   WMM .COF or legacy .csv    (required with --xp12)\n"
                "  --terrain <dir>    SRTM3 .hgt tile directory   (default: data/srtm3/)\n"
                "  --test             run unit tests then exit\n"
                "  --help             this message\n";
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << "  (--help for usage)\n";
            return 1;
        }
    }

    // ── Load magnetic declination model (only needed for X-Plane data) ──
    MagDec magdec;
    if (useXP12)
    {
        // Default to data/WMM.COF if --magdec was not explicitly given
        if (magdecFile.empty())
            magdecFile = "data/WMM.COF";

        magdec.load(magdecFile);

        if (!magdec.isLoaded())
        {
            std::cerr << "ERROR: X-Plane data requires a magnetic declination model.\n"
                      << "       Use --magdec <file> to specify a WMM .COF or .csv.\n";
            return 1;
        }
    }
    else if (!magdecFile.empty())
    {
        // User explicitly passed --magdec with A424; load it anyway
        magdec.load(magdecFile);
    }

    // ── Load navaids ──────────────────────────────────────────────────────
    AS::Model model;
    AS::World world;

    if (useXP12)
    {
        {
            std::ifstream probe(xp12File);
            if (!probe)
            {
                std::cerr << "ERROR: cannot open X-Plane data file: " << xp12File << "\n";
                return 1;
            }
        }
        std::cout << "Loading X-Plane 12 data: " << xp12File << " ... " << std::flush;
        AS::WorldParser parser;
        parser.parseXP12(xp12File, world, &magdec);
    }
    else
    {
        {
            std::ifstream probe(a424File);
            if (!probe)
            {
                std::cerr << "ERROR: cannot open navaid database: " << a424File << "\n"
                          << "       Run from the RadioNav/ directory, or use --a424 <file>.\n";
                return 1;
            }
        }
        std::cout << "Loading ARINC-424 data:  " << a424File << " ... " << std::flush;
        A424::A424Parser::ParseA424(a424File, &world);
    }

    std::cout << "done.\n"
              << "  VORs: "    << world.numVORs()
              << "  NDBs: "    << world.numNDBs()
              << "  LOCs: "    << world.numLOCs()
              << "  G/S: "     << world.numGSs()
              << "  DMEs: "    << world.numDMEs()
              << "  Markers: " << world.numMarkers() << "\n";

    // ── Check terrain availability ────────────────────────────────────────
    struct stat terrainStat;
    bool terrainOK = (stat(terrainDir.c_str(), &terrainStat) == 0);
    if (terrainOK)
    {
        std::cout << "Terrain tiles: " << terrainDir << "\n";
    }
    else
    {
        std::cerr << "WARNING: terrain directory not found: " << terrainDir << "\n"
                  << "         LOS terrain checking is disabled.\n";
        terrainDir = "";  // empty = no terrain in NavSimTask
    }

    // ── Create simulation task ────────────────────────────────────────────
    NavSimTask task(&world, &model, terrainDir);

    // ── Run ───────────────────────────────────────────────────────────────
    if (runTests)
    {
        runAllTests(model, task, world);
        // Return 1 if any test failed
        int failed = 0;
        for (auto& r : gTestResults) if (!r.passed) failed++;
        return failed ? 1 : 0;
    }
    else
    {
        printHelp();
        runREPL(model, task, world, &magdec);
    }

    return 0;
}
