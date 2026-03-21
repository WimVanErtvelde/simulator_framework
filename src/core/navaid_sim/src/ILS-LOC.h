#pragma once
#ifndef ILS_LOC_H
#define ILS_LOC_H

#include "LatLon.h"

namespace AS
{
	class ILS_LOC
	{
	public:
		ILS_LOC(
			float lat,
			float lon,
			float ele,
			int freq,
			float range,
			float truebearing,
			float magnbearing,
			std::string ident,
			std::string apt,
			std::string rw,
			std::string name
			)
		{
			mLatLon = LatLon(lat,lon);
			mElevation = ele;
			mFrequency = freq;
			mRange = range;
			mTrueBearing = truebearing;
			mMagnBearing = magnbearing;
			mIdent = ident;
			mAirport = apt;
			mRunway = rw;
			mName = name;
		}

		LatLon mLatLon;
		float mElevation;
		int mFrequency;
		float mRange;
		float mTrueBearing;
		float mMagnBearing;
		std::string mIdent;
		std::string mAirport;
		std::string mRunway;
		std::string mName;
	};
}
#endif