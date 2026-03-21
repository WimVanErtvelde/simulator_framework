#pragma once


#include "LatLon.h"

namespace AS
{
	class DME
	{
	public:		
		DME(float lat, float lon, float ele, int freq, float range, float bias, std::string ident, std::string name)
		{
			mLatLon = LatLon(lat, lon);
			mElevation = static_cast<float>(ft_to_m(ele));
			mFrequency = freq;
			mRange = range;
			mBias = bias;
			mIdent = ident;
			mName = name;
		}
		LatLon mLatLon;
		float mElevation;
		int mFrequency;
		float mRange;
		float mBias;
		std::string mIdent;
		std::string mName;
	};
}