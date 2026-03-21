#include "Model.h"
#include <cstring>

AS::Model::Model()
{
    std::memset(mFrequency,    0, sizeof(mFrequency));
    std::memset(mOBS,          0, sizeof(mOBS));
    std::memset(mVOR_Localizer,0, sizeof(mVOR_Localizer));
    std::memset(mVOR_Found,    0, sizeof(mVOR_Found));
    std::memset(mVOR_Deviation,0, sizeof(mVOR_Deviation));
    std::memset(mVOR_From,     0, sizeof(mVOR_From));
    std::memset(mVOR_Bearing,  0, sizeof(mVOR_Bearing));
    std::memset(mVOR_Distance, 0, sizeof(mVOR_Distance));
    std::memset(mVOR_DDM,      0, sizeof(mVOR_DDM));
    std::memset(mVOR_Ident,    0, sizeof(mVOR_Ident));
    std::memset(mVOR_CenteredOBS, 0, sizeof(mVOR_CenteredOBS));
    std::memset(mDME_Distance, 0, sizeof(mDME_Distance));
    std::memset(mDME_Found,    0, sizeof(mDME_Found));
    std::memset(mNDB_Found,    0, sizeof(mNDB_Found));
    std::memset(mNDB_Bearing,  0, sizeof(mNDB_Bearing));
    std::memset(mNDB_Distance, 0, sizeof(mNDB_Distance));
    std::memset(mNDB_Ident,    0, sizeof(mNDB_Ident));
    std::memset(mGS_Found,     0, sizeof(mGS_Found));
    std::memset(mGS_Deviation, 0, sizeof(mGS_Deviation));
    std::memset(mGS_Distance,  0, sizeof(mGS_Distance));
    std::memset(mLOC_Course,   0, sizeof(mLOC_Course));
}

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------
void AS::Model::setPosition(float lat_deg, float lon_deg, float alt_ft, float hdg_deg)
{
    mLat      = lat_deg;
    mLon      = lon_deg;
    mAltitude = alt_ft;
    mHeading  = hdg_deg;
}

void AS::Model::setFrequency(int radio, int freq)
{
    if (radio >= 0 && radio < RADIOS)
        mFrequency[radio] = freq;
}

void AS::Model::setOBS(int radio, int obs_deg)
{
    if (radio >= 0 && radio < RADIOS)
        mOBS[radio] = obs_deg;
}

void AS::Model::setADF_Frequency(int freq)
{
    mADF_Frequency = freq;
}

// ---------------------------------------------------------------------------
// Getters used by receivers
// ---------------------------------------------------------------------------
float AS::Model::getLat()      const { return mLat;      }
float AS::Model::getLon()      const { return mLon;      }
float AS::Model::getAltitude() const { return mAltitude; }
float AS::Model::getHeading()  const { return mHeading;  }

int AS::Model::getFrequency(int radio)    const { return mFrequency[radio]; }
int AS::Model::getADF_Frequency()         const { return mADF_Frequency;    }
int AS::Model::getOBS(int radio)          const { return mOBS[radio];       }

// ---------------------------------------------------------------------------
// Output read-back: build a RadioResult from stored arrays
// ---------------------------------------------------------------------------
AS::RadioResult AS::Model::getRadioResult(int radio) const
{
    RadioResult r;

    // VOR / LOC
    r.vor_found        = mVOR_Found    [radio];
    r.vor_localizer    = mVOR_Localizer[radio];
    r.vor_bearing      = mVOR_Bearing  [radio];
    r.vor_distance_m   = mVOR_Distance [radio];
    r.vor_deviation    = mVOR_Deviation[radio];
    r.vor_from         = mVOR_From     [radio];
    r.vor_centered_obs = mVOR_CenteredOBS[radio];
    r.vor_ddm          = mVOR_DDM      [radio];

    // Build ident string from character array
    for (int i = 0; i < IDENT_SIZE; ++i)
    {
        int c = mVOR_Ident[IDENT_SIZE * radio + i];
        if (c == 0) break;
        r.vor_ident += static_cast<char>(c);
    }

    // DME
    r.dme_found       = mDME_Found   [radio];
    r.dme_distance_nm = mDME_Distance[radio];

    // GS
    r.gs_found        = mGS_Found    [radio];
    r.gs_deviation    = mGS_Deviation[radio];
    r.gs_distance_m   = mGS_Distance [radio];

    // LOC course
    r.loc_course = mLOC_Course[radio];

    // NDB (ADF)
    r.ndb_found      = mNDB_Found   [radio];
    r.ndb_bearing    = mNDB_Bearing [radio];
    r.ndb_distance_m = mNDB_Distance[radio];
    for (int i = 0; i < IDENT_SIZE; ++i)
    {
        int c = mNDB_Ident[IDENT_SIZE * radio + i];
        if (c == 0) break;
        r.ndb_ident += static_cast<char>(c);
    }

    return r;
}

// ---------------------------------------------------------------------------
// Output setters (called by receivers)
// ---------------------------------------------------------------------------
void AS::Model::setVOR_Localizer(int radio, int v)    { mVOR_Localizer  [radio] = v; }
void AS::Model::setVOR_Found    (int radio, int v)    { mVOR_Found      [radio] = v; }
void AS::Model::setVOR_Deviation(int radio, float v)  { mVOR_Deviation  [radio] = v; }
void AS::Model::setVOR_From     (int radio, int v)    { mVOR_From       [radio] = v; }
void AS::Model::setVOR_Bearing  (int radio, float v)  { mVOR_Bearing    [radio] = v; }
void AS::Model::setVOR_Distance (int radio, float v)  { mVOR_Distance   [radio] = v; }
void AS::Model::setVOR_DDM      (int radio, float v)  { mVOR_DDM        [radio] = v; }
void AS::Model::setVOR_CenteredOBS(int radio, float v){ mVOR_CenteredOBS[radio] = v; }

void AS::Model::setVOR_Ident(int radio, int index, int c)
{
    mVOR_Ident[IDENT_SIZE * radio + index] = c;
}

void AS::Model::setNDB_Found   (int radio, int v)   { mNDB_Found   [radio] = v; }
void AS::Model::setNDB_Bearing (int radio, float v) { mNDB_Bearing [radio] = v; }
void AS::Model::setNDB_Distance(int radio, float v) { mNDB_Distance[radio] = v; }
void AS::Model::setNDB_Ident(int radio, int index, int v)
{
    mNDB_Ident[IDENT_SIZE * radio + index] = v;
}

void AS::Model::setGS_Found    (int radio, int v)   { mGS_Found    [radio] = v; }
void AS::Model::setGS_Deviation(int radio, float v) { mGS_Deviation[radio] = v; }
void AS::Model::setGS_Distance (int radio, float v) { mGS_Distance [radio] = v; }
void AS::Model::setLOC_Course  (int radio, float v) { mLOC_Course  [radio] = v; }

void AS::Model::setInnerMarker (int v) { mInnerMarker  = v; }
void AS::Model::setMiddleMarker(int v) { mMiddleMarker = v; }
void AS::Model::setOuterMarker (int v) { mOuterMarker  = v; }

void AS::Model::setDME_Distance(int radio, float v) { mDME_Distance[radio] = v; }
void AS::Model::setDME_Found   (int radio, int v)   { mDME_Found   [radio] = v; }
