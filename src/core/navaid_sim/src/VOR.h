#ifndef VOR_H
#define VOR_H

#include "LatLon.h"

namespace AS
{
	class VOR
	{
	public:		
		VOR(float lat,float lon,float ele,int freq,float range,float biasvariation,std::string ident,std::string name)//, bool isVOR)
		{
			mLatLon = LatLon(lat,lon);
			mElevation = ele;
			mFrequency = freq;
			mRange = range;
			mVariation = biasvariation;
			//mBias = biasvariation;
			mIdent = ident;
			mName = name;
			//mHasVOR = isVOR;
			//mHasDME = !isVOR;
		}
		/*bool isColocated(const VOR& v) {
			return (mFrequency == v.mFrequency && mIdent == v.mIdent && fabs(mLatLon.get_lat_deg() - v.mLatLon.get_lat_deg()) < 0.01 && fabs(mLatLon.get_lon_deg() - v.mLatLon.get_lon_deg()) < 0.01);
		}
		void merge(const VOR& v) { 
			if (v.mHasDME) { // merging DME into VOR
				mBias = v.mVariation;
				mHasDME = true;
			}
			else { // merging VOR into DME
				mBias = mVariation;
				mVariation = v.mVariation;
				mHasVOR = true;
			}
		}*/
		LatLon mLatLon;
		float mElevation;
		int mFrequency;
		float mRange;
		float mVariation;
		//float mBias;
		std::string mIdent;
		std::string mName;
		//bool mHasVOR = false;
		//bool mHasDME = false;
	};
}

#endif