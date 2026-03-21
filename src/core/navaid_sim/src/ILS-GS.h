#pragma once
#ifndef ILS_GS_H
#define ILS_GS_H

#include "LatLon.h"

namespace AS
{
	class ILS_GS
	{
	public:
		ILS_GS(
			float lat,
			float lon,
			float ele,
			int freq,
			float range,
			float bearing,
			float angle,
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
			mBearing = bearing;
			mAngle = angle;
			mIdent = ident;
			mAirport = apt;
			mRunway = rw;
			mName = name;
		}

		LatLon mLatLon;
		float mElevation;
		int mFrequency;
		float mRange;
		float mBearing;
		float mAngle;
		std::string mIdent;
		std::string mAirport;
		std::string mRunway;
		std::string mName;

		float getAngle(LatLon position,float ele)
		{
			float distance_nm = (float)position.get_distance_nm(mLatLon);
			float distance_m = (float)nm_to_m(distance_nm);
			float alt_diff = ele - (float)ft_to_m(mElevation);
			return (float)rad_to_deg(atan2(alt_diff,distance_m));
		}
	};
}

#endif