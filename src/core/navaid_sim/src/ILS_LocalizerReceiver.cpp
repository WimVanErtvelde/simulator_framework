#include "ILS_LocalizerReceiver.h"

AS::ILS_LocalizerReceiver::ILS_LocalizerReceiver(AS::World* world,Model* model) : AS::AbstractReceiver(world,model)
{
}


void AS::ILS_LocalizerReceiver::updateRadio(int radio)
{
	// only update if we're on a localizer frequency
	if(isLocalizer(mModel->getFrequency(radio)))
	{
		updateRadioLoc(radio);
	}
}

float AS::ILS_LocalizerReceiver::getDeviation(LatLon position,ILS_LOC loc)
{
	// calculate which radial we're on
	float radial = adjustHeading((float) loc.mLatLon.get_course_deg(position));
	
	// calculate expected radial
	float expected_radial = adjustHeading(loc.mTrueBearing + 180);

	// calculated deviation (positive means we're to the right of the radial)
	float deviation = adjustDeviation(radial - expected_radial);

	// we are only interested in the magnitude of the deviation
	return fabs(deviation);
}


void AS::ILS_LocalizerReceiver::updateRadioLoc(int radio)
{
	// results
	int found = 0;
	float deviation = 0;
	int from = 0;
	float distance = 0;
	float bearing = 0;
	float ddm = 0;
	int ident[IDENT_SIZE];
	float course = 0;
	for(int i = 0; i < IDENT_SIZE;i++)
	{
		// clear initial ident
		ident[i] = 0;
	}

	// get aircraft and radio parameters
	LatLon position(mModel->getLat(),mModel->getLon());
	int frequency = mModel->getFrequency(radio);
	
	// get all ILS localizers with the given frequency within range
	std::vector<ILS_LOC> candidates = mWorld->getILS_LOC(frequency,position);

	if(candidates.size() > 0)
	{
		// we assume that the localizer is valid, until proven otherwise
		found = 1;

		// select the first localizer that matches
		ILS_LOC loc = candidates[0];
		float bestDeviation = getDeviation(position,loc);

		// check whether there is another ILS LOC for which we are better aligned
		// (in case both sides of the same runway have an ILS with the same frequency, like 34R and 16L at DIA)
		for(unsigned int i = 1; i < candidates.size(); i++)
		{
			float newDeviation = getDeviation(position,candidates[i]);
			if(newDeviation < bestDeviation)
			{
				bestDeviation = newDeviation;
				loc = candidates[i];	
			}
		}

		// calculate which radial we are on
		float radial = adjustHeading((float)loc.mLatLon.get_course_deg(position));

		// calculate bearing
		bearing = adjustHeading(position.get_course_deg(loc.mLatLon));
	
		// calculate expected radial
		float expected_radial = adjustHeading(loc.mTrueBearing + 180);

		// calculated deviation (positive means we're to the right of the radial)
		deviation = adjustDeviation(radial - expected_radial);

		// determine to FROM/TO flag
		from = (fabs(deviation) > 90.0) ? 1 : 0;

		// adjust deviation to range
		if(deviation >= 90) deviation = 180 - deviation;
		if(deviation <= -90) deviation = -(180 + deviation);

		// calculate difference in depth modulation (DDM)
		float beacon_distance_nm = position.get_distance_nm(loc.mLatLon);
		distance = (float) nm_to_m(beacon_distance_nm);
		float centerline_distance = (float)sin(deg_to_rad(deviation)) * distance;
		ddm = (float) centerline_distance * 0.00145f; // DDM linear with 0.145% per meter

		// get course
		course = loc.mMagnBearing;

		// we can't receive the beacon if the deviation is greater than 35 deg within 10 nm, or greater than 10 deg outside
		if(beacon_distance_nm < 10)
		{
			if(fabs(deviation) > 35)
			{
				found = 0;
			}
		}
		else
		{
			if(fabs(deviation) > 10)
			{
				found = 0;
			}
		}

		// terrain line-of-sight check (VHF — strict LOS required)
		// loc.mElevation is in feet
		if(found && mLOS)
		{
			if(!mLOS->hasLOS(position, mModel->getAltitude(), loc.mLatLon, loc.mElevation))
				found = 0;
		}

		if(found > 0)
		{
			// copy name
			for(unsigned int i = 0; i < IDENT_SIZE && i < loc.mIdent.size(); i++)
			{
				ident[i] = (int) loc.mIdent[i];
			}
		}
		else
		{
			// reset values
			deviation = 0;
			ddm = 0;
			from = 0;
			bearing = 0;
			distance = 0;
		}
	}	

	mModel->setVOR_Localizer(radio,1);
	mModel->setVOR_Found(radio,found);
	mModel->setVOR_Deviation(radio,deviation);
	mModel->setVOR_DDM(radio,ddm);
	mModel->setVOR_From(radio,from);
	mModel->setVOR_Bearing(radio,bearing);
	mModel->setVOR_Distance(radio,distance);
	mModel->setLOC_Course(radio, course);

	// send identifier (e.g. "IAPA") to Ice
	for(int i = 0; i < IDENT_SIZE; i++)
	{
		mModel->setVOR_Ident(radio,i,ident[i]);
	}
}