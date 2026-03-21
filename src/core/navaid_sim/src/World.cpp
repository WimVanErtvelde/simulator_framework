#include "World.h"
#include <fstream>
#include <algorithm>
using namespace std;

void AS::World::addVOR(AS::VOR vor)
{
	/*auto range = mVORs.equal_range(vor.mFrequency);
	for (auto it = range.first; it != range.second; ++it) {
		if (it->second.isColocated(vor)) {
			it->second.merge(vor);
			return;
		}
	}*/

	mVORs.insert(make_pair(vor.mFrequency,vor));
}

std::vector<AS::VOR> AS::World::getVOR(int freq,LatLon position)
{
	std::vector<AS::VOR> result;

	multimap<int,AS::VOR>::iterator iter = mVORs.find(freq);

	if(iter != mVORs.end())
	{
		multimap<int,AS::VOR>::iterator last = mVORs.upper_bound(freq);
		for (; iter != last; ++iter)
		{
			if(position.get_distance_nm(iter->second.mLatLon) <= iter->second.mRange)
			{
					result.push_back(iter->second);
			}
		}
	}

	return result;
}

void AS::World::addNDB(AS::NDB ndb)
{
	mNDBs.insert(make_pair(ndb.mFrequency, ndb));
	//mNDBs.AddToTileGrid(ndb.mFrequency, ndb);
}

std::vector<AS::NDB> AS::World::getNDB(int freq, LatLon position)
{
	std::vector<AS::NDB> result;

	multimap<int, AS::NDB>::iterator iter = mNDBs.find(freq);

	if (iter != mNDBs.end())
	{
		multimap<int, AS::NDB>::iterator last = mNDBs.upper_bound(freq);
		for (; iter != last; ++iter)
		{
			float debug = (position.get_distance_nm(iter->second.mLatLon));
			if (debug <= iter->second.mRange)
			{
				result.push_back(iter->second);
			}
		}
	}
	/*std::vector<Tile<int, AS::NDB>*> tiles;
	mNDBs.GetTiles(position, 100, tiles);

	for (auto& tile : tiles)
		for (auto& pt : tile->mPoints)
			if (pt.first == freq)
				result.push_back(pt.second);*/

	return result;
}

void AS::World::addILS_LOC(AS::ILS_LOC loc)
{
	mILS_LOCs.insert(make_pair(loc.mFrequency,loc));
}

std::vector<AS::ILS_LOC> AS::World::getILS_LOC(int freq,LatLon position)
{
	std::vector<AS::ILS_LOC> result;

	multimap<int,AS::ILS_LOC>::iterator iter = mILS_LOCs.find(freq);

	if(iter != mILS_LOCs.end())
	{
		multimap<int,AS::ILS_LOC>::iterator last = mILS_LOCs.upper_bound(freq);
		for (; iter != last; ++iter)
		{
			if(position.get_distance_nm(iter->second.mLatLon) <= iter->second.mRange)
			{
				result.push_back(iter->second);
			}
		}
	}

	return result;
}

void AS::World::addILS_GS(AS::ILS_GS gs)
{
	mILS_GSs.insert(make_pair(gs.mFrequency,gs));
}

std::vector<AS::ILS_GS> AS::World::getILS_GS(int freq,LatLon position)
{
	std::vector<AS::ILS_GS> result;

	multimap<int,AS::ILS_GS>::iterator iter = mILS_GSs.find(freq);

	if(iter != mILS_GSs.end())
	{
		multimap<int,AS::ILS_GS>::iterator last = mILS_GSs.upper_bound(freq);
		for (; iter != last; ++iter)
		{
			if(position.get_distance_nm(iter->second.mLatLon) <= iter->second.mRange)
			{
				result.push_back(iter->second);
			}
		}
	}

	return result;
}

void AS::World::addILS_Marker(AS::ILS_Marker marker)
{
	mILS_Markers.push_back(marker);
}

std::vector<AS::ILS_Marker>* AS::World::getILS_Markers()
{
	return &mILS_Markers;
}

void AS::World::addDME(DME dme)
{
	mDMEs.insert(make_pair(dme.mFrequency, dme));
}

std::vector<AS::DME> AS::World::getDME(int freq, LatLon position)
{
	std::vector<AS::DME> result;

	multimap<int, AS::DME>::iterator iter = mDMEs.find(freq);

	if (iter != mDMEs.end())
	{
		multimap<int, AS::DME>::iterator last = mDMEs.upper_bound(freq);
		for (; iter != last; ++iter)
		{
			if (position.get_distance_nm(iter->second.mLatLon) <= iter->second.mRange)
			{
				result.push_back(iter->second);
			}
		}
	}

	return result;
}

void AS::World::writeNavaidsToJSON()
{
	ofstream jsonFile("navaids.json");
	jsonFile << "[" << endl;

	struct NavaidEntry {
		std::string ident;
		std::string name;
		char navaidtype;
		double lat;
		double lon;
		int freq;
	};
	std::vector<NavaidEntry> navaidEntries;
	for (auto& vorEntry : mVORs) {
		NavaidEntry entry;
		entry.ident = vorEntry.second.mIdent;
		entry.name = vorEntry.second.mName;
		entry.navaidtype = 'V';
		entry.lat = vorEntry.second.mLatLon.get_lat_deg();
		entry.lon = vorEntry.second.mLatLon.get_lon_deg();
		entry.freq = vorEntry.second.mFrequency;		
		navaidEntries.push_back(entry);
	}
	for (auto& ndbEntry : mNDBs) {
		NavaidEntry entry;
		entry.ident = ndbEntry.second.mIdent;
		entry.name = ndbEntry.second.mName;
		entry.navaidtype = 'N';
		entry.lat = ndbEntry.second.mLatLon.get_lat_deg();
		entry.lon = ndbEntry.second.mLatLon.get_lon_deg();
		entry.freq = ndbEntry.second.mFrequency;
		navaidEntries.push_back(entry);
	}
	for (auto& dmeEntry : mDMEs) {
		NavaidEntry entry;
		entry.ident = dmeEntry.second.mIdent;
		entry.name = dmeEntry.second.mName;
		entry.navaidtype = 'D';
		entry.lat = dmeEntry.second.mLatLon.get_lat_deg();
		entry.lon = dmeEntry.second.mLatLon.get_lon_deg();
		entry.freq = dmeEntry.second.mFrequency;
		navaidEntries.push_back(entry);
	}
	for (auto& locEntry : mILS_LOCs) {
		NavaidEntry entry;
		entry.ident = locEntry.second.mIdent;
		entry.name = locEntry.second.mName;
		entry.navaidtype = 'L';
		entry.lat = locEntry.second.mLatLon.get_lat_deg();
		entry.lon = locEntry.second.mLatLon.get_lon_deg();
		entry.freq = locEntry.second.mFrequency;
		navaidEntries.push_back(entry);
	}
	for (auto& gsEntry : mILS_GSs) {
		NavaidEntry entry;
		entry.ident = gsEntry.second.mIdent;
		entry.name = gsEntry.second.mName;
		entry.navaidtype = 'G';
		entry.lat = gsEntry.second.mLatLon.get_lat_deg();
		entry.lon = gsEntry.second.mLatLon.get_lon_deg();
		entry.freq = gsEntry.second.mFrequency;
		navaidEntries.push_back(entry);
	}
	// sort navaids by ident
	std::sort(navaidEntries.begin(), navaidEntries.end(), [](const NavaidEntry& a, const NavaidEntry& b) {
		return a.ident < b.ident;
		});

	bool first = true;
	for (auto& entry : navaidEntries) {
		if (!first)
			jsonFile << "," << endl;
		else
			first = false;
		jsonFile << "  {" << endl;
		jsonFile << "    \"ident\": \"" << entry.ident << "\"," << endl;
		jsonFile << "    \"name\": \"" << entry.name << "\"," << endl;
		jsonFile << "    \"type\": \"" << entry.navaidtype << "\"," << endl;
		jsonFile << "    \"lat\": " << entry.lat << "," << endl;
		jsonFile << "    \"lon\": " << entry.lon << "," << endl;
		jsonFile << "    \"freq\": " << entry.freq << endl;
		jsonFile << "  }";
	}
	jsonFile << "]" << endl;

	jsonFile.close();
}
