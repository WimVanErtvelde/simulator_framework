#pragma once
#ifndef MAGDEC_H
#define MAGDEC_H

// Magnetic declination from WMM (World Magnetic Model).
//
// Loads a WMM.COF coefficient file and computes declination on-the-fly
// using degree-12 spherical harmonic expansion.  No intermediate CSV needed.
//
// Also supports the legacy CSV grid format for backward-compatibility.

#include <vector>
#include <string>

class MagDec
{
public:
    MagDec();

    // Load either a .COF coefficient file or a legacy .csv grid.
    // Auto-detects format.  Returns true on success.
    bool load(const std::string& path);

    // Return magnetic declination (degrees, positive = east) at the
    // given WGS-84 position.
    //   COF mode:  exact spherical-harmonic computation
    //   CSV mode:  bilinear interpolation over the precomputed grid
    float getDeclination(float lat_deg, float lon_deg) const;

    // True if load() succeeded.
    bool isLoaded() const { return mLoaded; }

private:
    // ── COF (spherical harmonic) mode ──────────────────────────────
    struct Coeff { int n; int m; double gnm; double hnm; double dgnm; double dhnm; };

    bool loadCOF(const std::string& path);
    float computeSH(float lat_deg, float lon_deg) const;

    std::vector<Coeff> mCoeffs;
    double mEpoch;
    double mYear;       // evaluation year (epoch + 0.5 by default; can be overridden)
    int    mNmax;       // max spherical-harmonic degree (12 for WMM)
    bool   mCOFMode;

    // ── CSV (legacy grid) mode ─────────────────────────────────────
    bool loadCSV(const std::string& path);
    float getWrappedValue(int row, int col) const;

    std::vector<std::vector<float>> mGrid;
    int   mRows;
    int   mCols;
    float mLatStep;
    float mLonStep;
    float mLatMin;
    float mLonMin;

    bool  mLoaded;
};

#endif
