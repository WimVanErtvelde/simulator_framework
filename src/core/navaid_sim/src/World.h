#pragma once
// The world class holds all the beacons

#ifndef AS_WORLD_H
#define AS_WORLD_H

#include <map>
#include "VOR.h"
#include "NDB.h"
#include "DME.h"
#include "ILS-LOC.h"
#include "ILS-GS.h"
#include "ILS-Marker.h"
#include "LatLon.h"
#include <vector>
#include <sstream>
#include "TileGrid.h"

namespace AS
{
	class World
	{
	public:
		void addVOR(VOR vor);
		std::vector<VOR> getVOR(int freq,LatLon position);

		void addNDB(NDB ndb);
		std::vector<NDB> getNDB(int freq, LatLon position);

		void addILS_LOC(ILS_LOC loc);
		std::vector<ILS_LOC> getILS_LOC(int freq,LatLon position);

		void addILS_GS(ILS_GS gs);
		std::vector<ILS_GS> getILS_GS(int freq,LatLon position);

		void addILS_Marker(ILS_Marker marker);
		std::vector<ILS_Marker>* getILS_Markers();

		void addDME(DME dme);
		std::vector<DME> getDME(int freq, LatLon position);


		// debug function to write all navaids to a JSON file
		void writeNavaidsToJSON();

		// ── Test / introspection helpers ─────────────────────────────────
		// Return the first navaid of each type (useful for unit tests).
		// Returns true when an entry exists; leaves 'out' unchanged on false.
		bool getFirstVOR(VOR& out) const {
			if (mVORs.empty()) return false;
			out = mVORs.begin()->second; return true;
		}
		bool getFirstNDB(NDB& out) const {
			if (mNDBs.empty()) return false;
			out = mNDBs.begin()->second; return true;
		}
		bool getFirstLOC(ILS_LOC& out) const {
			if (mILS_LOCs.empty()) return false;
			out = mILS_LOCs.begin()->second; return true;
		}
		bool getFirstGS(ILS_GS& out) const {
			if (mILS_GSs.empty()) return false;
			out = mILS_GSs.begin()->second; return true;
		}
		bool getFirstDME(DME& out) const {
			if (mDMEs.empty()) return false;
			out = mDMEs.begin()->second; return true;
		}
		// Find a LOC that has a matching G/S on the same frequency (full ILS).
		bool getFirstILS(ILS_LOC& outLOC, ILS_GS& outGS) const {
			for (auto it = mILS_LOCs.begin(); it != mILS_LOCs.end(); ++it) {
				auto gs_it = mILS_GSs.find(it->first);
				if (gs_it != mILS_GSs.end()) {
					outLOC = it->second;
					outGS  = gs_it->second;
					return true;
				}
			}
			return false;
		}

		// Find nearest VOR to a position (any frequency). Returns false if DB is empty.
		bool findNearestVOR(LatLon position, VOR& out, double& dist_nm) const {
			bool found = false;
			double best = 1e9;
			for (auto& kv : mVORs) {
				double d = position.get_distance_nm(kv.second.mLatLon);
				if (d < best) { best = d; out = kv.second; found = true; }
			}
			dist_nm = best;
			return found;
		}

		// Find a VOR by ident (first match). Returns false if not found.
		bool findVORByIdent(const std::string& ident, VOR& out) const {
			for (auto& kv : mVORs) {
				if (kv.second.mIdent == ident) { out = kv.second; return true; }
			}
			return false;
		}

		size_t numVORs()     const { return mVORs.size();     }
		size_t numNDBs()     const { return mNDBs.size();     }
		size_t numLOCs()     const { return mILS_LOCs.size(); }
		size_t numGSs()      const { return mILS_GSs.size();  }
		size_t numDMEs()     const { return mDMEs.size();     }
		size_t numMarkers()  const { return mILS_Markers.size(); }

	private:
		std::multimap<int, VOR> mVORs;
		std::multimap<int, NDB> mNDBs;
		//TileGrid<int, NDB> mNDBs;
		std::multimap<int,ILS_LOC> mILS_LOCs;
		std::multimap<int,ILS_GS> mILS_GSs;
		std::vector<ILS_Marker> mILS_Markers;
		std::multimap<int, DME> mDMEs;
	};
}

#endif