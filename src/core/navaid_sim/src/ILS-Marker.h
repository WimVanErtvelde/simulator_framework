#pragma once
#ifndef AS_ILS_MARKER_H
#define AS_ILS_MARKER_H

#include "LatLon.h"

namespace AS
{
	class ILS_Marker
	{
	public:
		ILS_Marker(float lat,float lon,float ele,float heading,std::string apt,std::string rwy,std::string type)
		{
			mLatLon = LatLon(lat,lon);
			mElevation = ele;
			mHeading = heading;
			mAirport = apt;
			mRunway = rwy;
			mType = type;
		}
		LatLon mLatLon;
		float mElevation;
		float mHeading;
		std::string mAirport;
		std::string mRunway;
		std::string mType;
	};
}

#endif