#pragma once
#ifndef AS_MODEL_H
#define AS_MODEL_H

#include <string>

#define RADIOS     4   // index 0 unused; radios at indices 1, 2, 3
#define IDENT_SIZE 6   // VOR idents = 3 chars, LOC idents up to 6 (e.g. "I-ABCD")

namespace AS
{
    namespace Radios
    {
        const int radio_1 = 1;
        const int radio_2 = 2;
        const int cdi     = 3;
    }

    // -----------------------------------------------------------------------
    // RadioResult — snapshot of one radio's output after step()
    // -----------------------------------------------------------------------
    struct RadioResult
    {
        // VOR / LOC
        int   vor_found        = 0;
        int   vor_localizer    = 0;   // 0 = VOR, 1 = LOC
        float vor_bearing      = 0;   // degrees, magnetic
        float vor_distance_m   = 0;   // ground distance, metres
        float vor_deviation    = 0;   // degrees; positive = right of radial
        int   vor_from         = 0;   // 0 = TO, 1 = FROM
        float vor_centered_obs = 0;
        float vor_ddm          = 0;
        std::string vor_ident;         // e.g. "BUB" or "I-BRU"

        // DME
        int   dme_found        = 0;
        float dme_distance_nm  = 0;   // slant range, NM

        // Glideslope
        int   gs_found         = 0;
        float gs_deviation     = 0;   // degrees; positive = above glidepath
        float gs_distance_m    = 0;

        // LOC course
        float loc_course       = 0;   // magnetic course of the localizer

        // NDB / ADF (only meaningful for radio 1)
        int   ndb_found        = 0;
        float ndb_bearing      = 0;   // degrees, magnetic
        float ndb_distance_m   = 0;
        std::string ndb_ident;
    };

    // -----------------------------------------------------------------------
    // Model — aircraft state + radio I/O, no external bus dependency
    // -----------------------------------------------------------------------
    class Model
    {
    public:
        Model();
        ~Model() = default;

        // ── Inputs (set by host / test code before each step) ────────────

        // alt_ft  : altitude MSL in feet
        // hdg_deg : true heading in degrees (optional, default 0)
        void setPosition(float lat_deg, float lon_deg, float alt_ft, float hdg_deg = 0.f);

        // freq: MHz * 100  (e.g. 11580 = 115.80 MHz)
        void setFrequency(int radio, int freq);

        // obs_deg: OBS course selector, degrees
        void setOBS(int radio, int obs_deg);

        // freq: kHz * 100  (e.g. 36200 = 362.0 kHz)
        void setADF_Frequency(int freq);

        // ── Getters used by receivers ────────────────────────────────────
        float getLat()           const;
        float getLon()           const;
        float getAltitude()      const;   // feet MSL
        float getHeading()       const;   // degrees true

        int   getFrequency(int radio)    const;   // MHz * 100 internal format
        int   getADF_Frequency()         const;   // kHz * 100 internal format
        int   getOBS(int radio)          const;

        // ── Output read-back (call after step) ───────────────────────────
        RadioResult getRadioResult(int radio) const;

        int getInnerMarker()  const { return mInnerMarker;  }
        int getMiddleMarker() const { return mMiddleMarker; }
        int getOuterMarker()  const { return mOuterMarker;  }

        // ── Output setters called by receivers ───────────────────────────
        void setVOR_Localizer   (int radio, int value);
        void setVOR_Found       (int radio, int value);
        void setVOR_Deviation   (int radio, float value);
        void setVOR_From        (int radio, int value);
        void setVOR_Ident       (int radio, int index, int character);
        void setVOR_Bearing     (int radio, float value);
        void setVOR_Distance    (int radio, float value);
        void setVOR_DDM         (int radio, float value);
        void setVOR_CenteredOBS (int radio, float value);

        void setNDB_Found   (int radio, int value);
        void setNDB_Bearing (int radio, float value);
        void setNDB_Distance(int radio, float value);
        void setNDB_Ident   (int radio, int index, int value);

        void setGS_Found    (int radio, int value);
        void setGS_Deviation(int radio, float value);
        void setGS_Distance (int radio, float value);

        void setLOC_Course(int radio, float value);

        void setInnerMarker (int value);
        void setMiddleMarker(int value);
        void setOuterMarker (int value);

        void setDME_Distance(int radio, float value);
        void setDME_Found   (int radio, int value);

    private:
        float mLat      = 0.f;
        float mLon      = 0.f;
        float mAltitude = 0.f;
        float mHeading  = 0.f;

        int mFrequency   [RADIOS]            = {};
        int mADF_Frequency                   = 0;
        int mOBS         [RADIOS]            = {};

        int   mVOR_Localizer  [RADIOS]           = {};
        int   mVOR_Found      [RADIOS]           = {};
        float mVOR_Deviation  [RADIOS]           = {};
        int   mVOR_From       [RADIOS]           = {};
        float mVOR_Bearing    [RADIOS]           = {};
        float mVOR_Distance   [RADIOS]           = {};
        float mVOR_DDM        [RADIOS]           = {};
        int   mVOR_Ident      [RADIOS * IDENT_SIZE] = {};
        float mVOR_CenteredOBS[RADIOS]           = {};

        float mDME_Distance[RADIOS] = {};
        int   mDME_Found   [RADIOS] = {};

        int   mNDB_Found   [RADIOS]              = {};
        float mNDB_Bearing [RADIOS]              = {};
        float mNDB_Distance[RADIOS]              = {};
        int   mNDB_Ident   [RADIOS * IDENT_SIZE] = {};

        int   mGS_Found    [RADIOS] = {};
        float mGS_Deviation[RADIOS] = {};
        float mGS_Distance [RADIOS] = {};
        float mLOC_Course  [RADIOS] = {};

        int mInnerMarker  = 0;
        int mMiddleMarker = 0;
        int mOuterMarker  = 0;
    };
}

#endif // AS_MODEL_H
