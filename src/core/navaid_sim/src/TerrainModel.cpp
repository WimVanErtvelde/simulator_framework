#include "TerrainModel.h"
#include "Units.h"       // m_to_ft()

#include <fstream>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------------------------
TerrainModel::TerrainModel(const std::string& tileDir)
    : mTileDir(tileDir)
{
    // Ensure the directory path ends with a separator
    if (!mTileDir.empty())
    {
        char last = mTileDir.back();
        if (last != '/' && last != '\\')
            mTileDir += '/';
    }
}

// ---------------------------------------------------------------------------
// makeKey / makeFilename
// ---------------------------------------------------------------------------
std::string TerrainModel::makeKey(int tileLat, int tileLon)
{
    std::ostringstream ss;
    ss << (tileLat >= 0 ? 'N' : 'S')
       << std::setw(2) << std::setfill('0') << std::abs(tileLat)
       << (tileLon >= 0 ? 'E' : 'W')
       << std::setw(3) << std::setfill('0') << std::abs(tileLon);
    return ss.str();
}

std::string TerrainModel::makeFilename(int tileLat, int tileLon)
{
    return makeKey(tileLat, tileLon) + ".hgt";
}

// ---------------------------------------------------------------------------
// evictOldest — remove least-recently-used tile from cache
// ---------------------------------------------------------------------------
void TerrainModel::evictOldest()
{
    auto oldest = mCache.begin();
    for (auto it = mCache.begin(); it != mCache.end(); ++it)
        if (it->second.lastAccess < oldest->second.lastAccess)
            oldest = it;
    mCache.erase(oldest);
}

// ---------------------------------------------------------------------------
// getTile — returns pointer to tile data (loads from disk if needed)
// ---------------------------------------------------------------------------
const TerrainModel::TileEntry* TerrainModel::getTile(int tileLat, int tileLon)
{
    std::string key = makeKey(tileLat, tileLon);

    // --- Cache hit ---
    auto it = mCache.find(key);
    if (it != mCache.end())
    {
        it->second.lastAccess = ++mAccessCounter;
        return &it->second;
    }

    // --- Load from disk ---
    std::string path = mTileDir + makeFilename(tileLat, tileLon);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return nullptr;   // tile not downloaded: ocean or outside coverage

    TileEntry entry;
    entry.data.resize(TILE_PIXELS);
    file.read(reinterpret_cast<char*>(entry.data.data()), TILE_PIXELS * sizeof(int16_t));

    if ((int)file.gcount() != TILE_PIXELS * (int)sizeof(int16_t))
        return nullptr;   // truncated / incomplete file — skip

    // HGT is big-endian; swap bytes on little-endian systems (x86/ARM PC)
    for (auto& v : entry.data)
    {
        uint8_t* b = reinterpret_cast<uint8_t*>(&v);
        std::swap(b[0], b[1]);
    }

    // Evict oldest tile if cache is full
    if (mCache.size() >= MAX_CACHE)
        evictOldest();

    entry.lastAccess = ++mAccessCounter;
    mCache[key] = std::move(entry);
    return &mCache[key];
}

// ---------------------------------------------------------------------------
// getElevationM — bilinear interpolation of terrain elevation in metres
// ---------------------------------------------------------------------------
float TerrainModel::getElevationM(double lat, double lon)
{
    // Tile identified by its SW corner
    int tileLat = (int)std::floor(lat);
    int tileLon = (int)std::floor(lon);

    const TileEntry* tile = getTile(tileLat, tileLon);
    if (!tile)
        return 0.0f;

    // Fractional pixel position within tile
    //   rowF = 0.0 → northern edge (lat = tileLat + 1)
    //   rowF = 600 → southern edge (lat = tileLat)
    double rowF = (tileLat + 1.0 - lat) * (TILE_SIZE - 1);
    double colF = (lon - tileLon)        * (TILE_SIZE - 1);

    // Clamp to valid pixel range
    int row0 = std::max(0, std::min(TILE_SIZE - 2, (int)rowF));
    int col0 = std::max(0, std::min(TILE_SIZE - 2, (int)colF));
    int row1 = row0 + 1;
    int col1 = col0 + 1;

    double dr = rowF - row0;
    double dc = colF - col0;

    // Sample helper: treat void (-32768) as 0 m (sea level)
    auto sample = [&](int r, int c) -> float {
        int16_t v = tile->data[static_cast<size_t>(r * TILE_SIZE + c)];
        return (v == (int16_t)VOID_VALUE) ? 0.0f : (float)v;
    };

    // Bilinear interpolation
    float h00 = sample(row0, col0);
    float h01 = sample(row0, col1);
    float h10 = sample(row1, col0);
    float h11 = sample(row1, col1);

    return (float)(  h00 * (1.0 - dr) * (1.0 - dc)
                   + h01 * (1.0 - dr) *        dc
                   + h10 *        dr  * (1.0 - dc)
                   + h11 *        dr  *        dc  );
}

// ---------------------------------------------------------------------------
float TerrainModel::getElevationFt(double lat, double lon)
{
    return (float)m_to_ft(getElevationM(lat, lon));
}

// ---------------------------------------------------------------------------
bool TerrainModel::hasTile(double lat, double lon)
{
    int tileLat = (int)std::floor(lat);
    int tileLon = (int)std::floor(lon);
    return getTile(tileLat, tileLon) != nullptr;
}
