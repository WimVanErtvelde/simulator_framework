#ifdef _MSC_VER
#  define _USE_MATH_DEFINES   // M_PI for MSVC
#endif

#include "MagDec.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <iostream>
#include <cstdlib>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// WGS-84 constants
// ─────────────────────────────────────────────────────────────────────────────
static const double WGS84_A  = 6378.137;                          // semi-major axis (km)
static const double WGS84_F  = 1.0 / 298.257223563;
static const double WGS84_B  = WGS84_A * (1.0 - WGS84_F);        // semi-minor axis
static const double WMM_RE   = 6371.2;                            // WMM reference radius (km)

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

MagDec::MagDec()
    : mEpoch(0.0), mYear(0.0), mNmax(12), mCOFMode(false)
    , mRows(0), mCols(0)
    , mLatStep(0.f), mLonStep(0.f)
    , mLatMin(0.f), mLonMin(0.f)
    , mLoaded(false)
{
}

// ─────────────────────────────────────────────────────────────────────────────
// Auto-detect and load
// ─────────────────────────────────────────────────────────────────────────────

bool MagDec::load(const std::string& path)
{
    // Detect format by extension
    std::string ext;
    {
        auto dot = path.rfind('.');
        if (dot != std::string::npos)
        {
            ext = path.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        }
    }

    if (ext == ".cof")
        return loadCOF(path);
    if (ext == ".csv")
        return loadCSV(path);

    // No recognised extension — try reading the first line to decide
    std::ifstream probe(path);
    if (!probe) { std::cerr << "MagDec: cannot open " << path << "\n"; return false; }

    std::string first;
    std::getline(probe, first);
    probe.close();

    // COF files start with the epoch year (e.g. "    2025.0")
    // CSV files start with a lat value   (e.g. "90.0,-180.0,...")
    if (first.find(',') != std::string::npos)
        return loadCSV(path);
    else
        return loadCOF(path);
}

// ─────────────────────────────────────────────────────────────────────────────
// COF loader
// ─────────────────────────────────────────────────────────────────────────────

bool MagDec::loadCOF(const std::string& path)
{
    std::ifstream input(path);
    if (!input) { std::cerr << "MagDec: cannot open " << path << "\n"; return false; }

    mCoeffs.clear();
    bool haveEpoch = false;

    std::string line;
    while (std::getline(input, line))
    {
        // Strip leading/trailing whitespace
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        line = line.substr(a);

        // End-of-file sentinel: line of 9's
        if (line.size() > 4 && line.substr(0, 5) == "99999")
            break;

        std::istringstream ss(line);

        if (!haveEpoch)
        {
            // First data line: epoch and model name
            ss >> mEpoch;
            haveEpoch = true;
            continue;
        }

        Coeff c{};
        ss >> c.n >> c.m >> c.gnm >> c.hnm >> c.dgnm >> c.dhnm;
        if (c.n == 0 && c.m == 0) continue;
        mCoeffs.push_back(c);
        if (c.n > mNmax) mNmax = c.n;
    }

    if (mCoeffs.empty())
    {
        std::cerr << "MagDec: no coefficients found in " << path << "\n";
        return false;
    }

    // Default evaluation year: epoch + 0.5 (model mid-point)
    mYear    = mEpoch + 0.5;
    mCOFMode = true;
    mLoaded  = true;

    std::cout << "MagDec: WMM COF epoch " << mEpoch
              << ", " << mCoeffs.size() << " coefficients (degree " << mNmax << ")"
              << ", eval year " << mYear << "\n";
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Spherical harmonic computation
// ─────────────────────────────────────────────────────────────────────────────

float MagDec::computeSH(float lat_deg, float lon_deg) const
{
    // Clamp poles to avoid singularity
    double lat_d = static_cast<double>(lat_deg);
    double lon_d = static_cast<double>(lon_deg);
    if (lat_d >  89.999) lat_d =  89.999;
    if (lat_d < -89.999) lat_d = -89.999;

    double dt = mYear - mEpoch;
    double lat_rad = lat_d * M_PI / 180.0;
    double lon_rad = lon_d * M_PI / 180.0;
    double sin_lat = std::sin(lat_rad);
    double cos_lat = std::cos(lat_rad);

    // ── Geodetic → geocentric conversion ──────────────────────────────────
    double a2 = WGS84_A * WGS84_A;
    double b2 = WGS84_B * WGS84_B;
    double D2 = a2 * cos_lat * cos_lat + b2 * sin_lat * sin_lat;
    double Rg = std::sqrt((a2 * a2 * cos_lat * cos_lat + b2 * b2 * sin_lat * sin_lat) / D2);
    // altitude = 0 km (surface)

    double b2_over_a2 = b2 / a2;
    double sin_lat_gc = b2_over_a2 * sin_lat
        / std::sqrt(b2_over_a2 * b2_over_a2 * sin_lat * sin_lat + cos_lat * cos_lat);
    double cos_lat_gc = std::sqrt(1.0 - sin_lat_gc * sin_lat_gc);
    if (cos_lat >= 0.0)
        cos_lat_gc =  std::fabs(cos_lat_gc);
    else
        cos_lat_gc = -std::fabs(cos_lat_gc);

    double lat_gc_rad = std::asin(sin_lat_gc);

    double ratio = WMM_RE / Rg;

    // ── Associated Legendre polynomials (Schmidt semi-normalized) ─────────
    int N = mNmax + 2;
    // Use flat arrays for P[n][m] and dP[n][m]
    std::vector<double> P(static_cast<size_t>(N * N), 0.0);
    std::vector<double> dP(static_cast<size_t>(N * N), 0.0);

    auto idx = [N](int n, int m) -> size_t { return static_cast<size_t>(n * N + m); };

    P[idx(0,0)]  = 1.0;
    P[idx(1,0)]  = sin_lat_gc;
    P[idx(1,1)]  = cos_lat_gc;
    dP[idx(0,0)] = 0.0;
    dP[idx(1,0)] = cos_lat_gc;
    dP[idx(1,1)] = -sin_lat_gc;

    for (int n = 2; n <= mNmax; ++n)
    {
        for (int m = 0; m <= n; ++m)
        {
            if (m == n)
            {
                P[idx(n,m)]  = cos_lat_gc * P[idx(n-1,m-1)];
                dP[idx(n,m)] = cos_lat_gc * dP[idx(n-1,m-1)] - sin_lat_gc * P[idx(n-1,m-1)];
            }
            else if (m == n - 1)
            {
                P[idx(n,m)]  = sin_lat_gc * P[idx(n-1,m)];
                dP[idx(n,m)] = sin_lat_gc * dP[idx(n-1,m)] + cos_lat_gc * P[idx(n-1,m)];
            }
            else
            {
                double K = (static_cast<double>((n-1)*(n-1) - m*m))
                         / (static_cast<double>((2*n-1)*(2*n-3)));
                P[idx(n,m)]  = sin_lat_gc * P[idx(n-1,m)] - K * P[idx(n-2,m)];
                dP[idx(n,m)] = sin_lat_gc * dP[idx(n-1,m)] + cos_lat_gc * P[idx(n-1,m)] - K * dP[idx(n-2,m)];
            }
        }
    }

    // ── Schmidt normalization factors ─────────────────────────────────────
    std::vector<double> S(static_cast<size_t>(N * N), 0.0);
    S[idx(0,0)] = 1.0;
    for (int n = 1; n <= mNmax; ++n)
    {
        S[idx(n,0)] = S[idx(n-1,0)] * (2.0 * n - 1.0) / static_cast<double>(n);
        for (int m = 1; m <= n; ++m)
        {
            if (m == 1)
                S[idx(n,m)] = S[idx(n,m-1)] * std::sqrt(2.0 / (static_cast<double>(n) * (static_cast<double>(n) + 1.0)));
            else
                S[idx(n,m)] = S[idx(n,m-1)] * std::sqrt((static_cast<double>(n - m + 1)) / (static_cast<double>(n + m)));
        }
    }

    // ── Sum field components in geocentric frame ──────────────────────────
    double Br = 0.0;   // radial  (outward)
    double Bt = 0.0;   // theta   (southward)
    double Bp = 0.0;   // phi     (eastward)

    for (const auto& c : mCoeffs)
    {
        if (c.n > mNmax) continue;
        double g = c.gnm + dt * c.dgnm;
        double h = c.hnm + dt * c.dhnm;

        double rr = std::pow(ratio, c.n + 2);
        double cos_m = std::cos(static_cast<double>(c.m) * lon_rad);
        double sin_m = std::sin(static_cast<double>(c.m) * lon_rad);

        double Pnm  = P[idx(c.n, c.m)]  * S[idx(c.n, c.m)];
        double dPnm = dP[idx(c.n, c.m)] * S[idx(c.n, c.m)];

        Br += static_cast<double>(c.n + 1) * rr * (g * cos_m + h * sin_m) * Pnm;
        Bt -= rr * (g * cos_m + h * sin_m) * dPnm;
        if (cos_lat_gc != 0.0)
            Bp -= rr * static_cast<double>(c.m) * (-g * sin_m + h * cos_m) * Pnm / cos_lat_gc;
    }

    // ── Rotate geocentric → geodetic ──────────────────────────────────────
    double psi = lat_rad - lat_gc_rad;
    double Bx  = Bt * std::cos(psi) - Br * std::sin(psi);   // north
    double By  = Bp;                                          // east

    // ── Declination ───────────────────────────────────────────────────────
    double decl = std::atan2(By, Bx) * 180.0 / M_PI;
    return static_cast<float>(decl);
}

// ─────────────────────────────────────────────────────────────────────────────
// getDeclination — dispatch to appropriate mode
// ─────────────────────────────────────────────────────────────────────────────

float MagDec::getDeclination(float lat_deg, float lon_deg) const
{
    if (!mLoaded) return 0.f;

    if (mCOFMode)
        return computeSH(lat_deg, lon_deg);

    // CSV grid: bilinear interpolation
    float rowF = (lat_deg - mLatMin) / mLatStep;
    float colF = (lon_deg - mLonMin) / mLonStep;

    int r0 = static_cast<int>(std::floor(rowF));
    int c0 = static_cast<int>(std::floor(colF));
    int r1 = r0 + 1;
    int c1 = c0 + 1;

    float fr = rowF - static_cast<float>(r0);
    float fc = colF - static_cast<float>(c0);

    float q00 = getWrappedValue(r0, c0);
    float q10 = getWrappedValue(r0, c1);
    float q01 = getWrappedValue(r1, c0);
    float q11 = getWrappedValue(r1, c1);

    float f0 = q00 * (1.f - fc) + q10 * fc;
    float f1 = q01 * (1.f - fc) + q11 * fc;

    return f0 * (1.f - fr) + f1 * fr;
}

// ─────────────────────────────────────────────────────────────────────────────
// CSV grid loader (legacy)
// ─────────────────────────────────────────────────────────────────────────────

bool MagDec::loadCSV(const std::string& path)
{
    std::ifstream input(path.c_str());
    if (!input) { std::cerr << "MagDec: cannot open " << path << "\n"; return false; }

    struct Sample { float lat; float lon; float dec; };
    std::vector<Sample> samples;
    samples.reserve(270000);

    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tok;

        std::getline(ss, tok, ',');
        float lat = static_cast<float>(std::atof(tok.c_str()));
        std::getline(ss, tok, ',');
        float lon = static_cast<float>(std::atof(tok.c_str()));
        std::getline(ss, tok, ',');
        float dec = static_cast<float>(std::atof(tok.c_str()));

        samples.push_back({ lat, lon, dec });
    }

    if (samples.empty()) { std::cerr << "MagDec: CSV is empty\n"; return false; }

    float firstLat = samples[0].lat;
    size_t colsSz = 0;
    for (size_t i = 0; i < samples.size() && samples[i].lat == firstLat; ++i)
        colsSz++;

    if (colsSz == 0 || (samples.size() % colsSz) != 0)
    {
        std::cerr << "MagDec: irregular grid (" << samples.size()
                  << " samples, " << colsSz << " cols)\n";
        return false;
    }
    size_t rowsSz = samples.size() / colsSz;

    float latMax = samples.front().lat;
    float latMin = samples[samples.size() - colsSz].lat;
    float lonMin = samples[0].lon;
    float lonMax = samples[colsSz - 1].lon;

    mRows    = static_cast<int>(rowsSz);
    mCols    = static_cast<int>(colsSz);
    mLatStep = (mRows > 1) ? std::fabs(latMax - latMin) / static_cast<float>(mRows - 1) : 1.f;
    mLonStep = (mCols > 1) ? std::fabs(lonMax - lonMin) / static_cast<float>(mCols - 1) : 1.f;
    mLatMin  = latMin;
    mLonMin  = lonMin;

    mGrid.resize(rowsSz, std::vector<float>(colsSz, 0.f));
    for (size_t r = 0; r < rowsSz; ++r)
    {
        size_t csvRow = (rowsSz - 1) - r;
        for (size_t c = 0; c < colsSz; ++c)
            mGrid[r][c] = samples[csvRow * colsSz + c].dec;
    }

    mCOFMode = false;
    mLoaded  = true;

    std::cout << "MagDec: " << mRows << "x" << mCols
              << " grid, step=" << mLatStep << "\xC2\xB0x" << mLonStep << "\xC2\xB0"
              << ", lat " << latMin << ".." << latMax
              << ", lon " << lonMin << ".." << lonMax << "\n";
    return true;
}

float MagDec::getWrappedValue(int row, int col) const
{
    if (row < 0)       row = 0;
    if (row >= mRows)  row = mRows - 1;
    col = col % mCols;
    if (col < 0) col += mCols;
    return mGrid[static_cast<size_t>(row)][static_cast<size_t>(col)];
}
