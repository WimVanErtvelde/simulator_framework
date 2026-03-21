#pragma once
#ifndef NDB_H
#define NDB_H

#include "LatLon.h"

namespace AS
{
	class NDB
	{
	public:
		NDB(float lat,float lon,int freq,float range,std::string ident,std::string name)
		{
			mLatLon = LatLon(lat,lon);
			mFrequency = freq;
			mRange = range;
			mIdent = ident;
			mName = name;
		}
		LatLon mLatLon;
		int mFrequency;
		float mRange;
		std::string mIdent;
		std::string mName;
	};
}

#endif