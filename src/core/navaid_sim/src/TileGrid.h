#pragma once
#include "LatLon.h"
#include <array>
#include <vector>
#include <map>

template<class KeyType, class PointType>
class Tile {
public:
	void add(KeyType key, PointType pt);
	std::multimap<KeyType, PointType> mPoints;
};
template<class KeyType, class PointType>
class TileGrid
{
public:
	void AddToTileGrid(KeyType key, PointType pt);
	void GetTiles(const LatLon& latlon, int rangenm, std::vector<Tile<KeyType, PointType>*>& tiles);
	static int CoordinateToTileIndex(const LatLon& latlon);
private:
	static inline const int rowcount = 180;
	static inline const int colcount = 360;
	static inline const int rowheight = 180 / rowcount;
	static inline const int colheight = 180 / colcount;
	std::array<Tile<KeyType, PointType>, rowcount * colcount> mGrid;
};

template<class KeyType, class PointType>
inline void TileGrid<KeyType, PointType>::AddToTileGrid(KeyType key, PointType pt)
{
	int index = CoordinateToTileIndex(pt.mLatLon);
	mGrid[index].add(key, pt);
}

template<class KeyType, class PointType>
inline void TileGrid<KeyType, PointType>::GetTiles(const LatLon& latlon, int rangenm, std::vector<Tile<KeyType, PointType>*>& tiles)
{
	int spanLat = (rangenm / 60) + 1;
	int spanLon = ((float(rangenm) / cosf(latlon.get_lat_rad())) / 60) + 1;
	int indexpt = CoordinateToTileIndex(latlon);
	int indexbottom = indexpt - spanLat;
	int indextop = indexpt + spanLat;
	int indexleft = indexpt - spanLon;
	int indexright = indexpt + spanLon;
	if (indextop >= rowcount) indextop = rowcount - 1;
	if (indexbottom < 0) indexbottom = 0;
	if (indexright >= colcount) indextop = colcount - 1;
	if (indexleft < 0) indexleft = 0;
	tiles.clear();
	for (int row = indexbottom; row <= indextop; row++)
		for (int col = indexleft; row <= indexright; row++)
			tiles.push_back(&(mGrid[row * colcount + col]));
}

template<class KeyType, class PointType>
inline int TileGrid<KeyType, PointType>::CoordinateToTileIndex(const LatLon& latlon)
{
	int lat = latlon.get_lat_deg() + 90.f;
	int lon = latlon.get_lon_deg() + 180.f;
	int indexlat = lat / (rowheight);
	int indexlon = lon / (colheight);
	return indexlat * colcount + indexlon;
}

template<class KeyType, class PointType>
inline void Tile<KeyType, PointType>::add(KeyType key, PointType pt)
{
	mPoints.insert(key, pt);
}
