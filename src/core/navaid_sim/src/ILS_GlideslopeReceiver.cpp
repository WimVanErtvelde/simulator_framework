#include "ILS_GlideslopeReceiver.h"

AS::ILS_GlideslopeReceiver::ILS_GlideslopeReceiver(AS::World* world,Model* model) : AS::AbstractReceiver(world,model)
{
}

float AS::ILS_GlideslopeReceiver::getDeviation(LatLon position,ILS_GS gs)
{
	// calculate which radial we're on
	float radial = adjustHeading((float)gs.mLatLon.get_course_deg(position));
	
	// calculate expected radial
	float expected_radial = adjustHeading(gs.mBearing + 180);

	// calculated deviation (positive means we're to the right of the radial)
	float deviation = adjustDeviation(radial - expected_radial);

	// we are only interested in the magnitude of the deviation
	return fabs(deviation);
}

void AS::ILS_GlideslopeReceiver::updateRadio(int radio)
{
	// results
	int found = 0;
	float deviation = 0;
	float distance = 0;

	// get aircraft and radio parameters
	LatLon position(mModel->getLat(),mModel->getLon());
	int frequency = mModel->getFrequency(radio);

	// get all glideslopes within range for the given frequency
	std::vector<ILS_GS> candidates = mWorld->getILS_GS(frequency,position);

	if(candidates.size() > 0)
	{
		// we assume that the glideslope is valid, until proven otherwise
		found = 1;

		// select first glideslope that matches
		ILS_GS gs = candidates[0];
		float bestDeviation = getDeviation(position,gs);

		// check whether there is another ILS GS for which we are better aligned
		// (in case both sides of the same runway have an ILS with the same frequency, like 34R and 16L at DIA)
		for(unsigned int i = 1; i < candidates.size(); i++)
		{
			float newDeviation = getDeviation(position,candidates[i]);
			if(newDeviation < bestDeviation)
			{
				bestDeviation = newDeviation;
				gs = candidates[i];
			}
		}

		// calculate which radial we are on
		float radial = adjustHeading((float)gs.mLatLon.get_course_deg(position));

		// float bearing = adjustHeading(position.get_course_deg(gs.mLatLon));  // unused

		// calculate expected radial
		float expected_radial = adjustHeading(gs.mBearing + 180);

		// calculated deviation (positive means we're to the right of the radial)
		deviation = adjustDeviation(radial - expected_radial);

		// glideslopes only works in one direction of the runway
		if(fabs(deviation) > 90.0)
		{
			found = 0;
		}

		// terrain line-of-sight check (UHF — strict LOS required)
		// gs.mElevation is in feet (confirmed: getAngle() calls ft_to_m(mElevation))
		if(found && mLOS)
		{
			if(!mLOS->hasLOS(position, mModel->getAltitude(), gs.mLatLon, gs.mElevation))
				found = 0;
		}

		// no glideslope when aircraft is pointing away more than 120 deg from runway heading (requested by Neil)
		/*if (radio != 3) // CDI does show GS regardless of aircraft heading
		{
			float heading = adjustHeading(mModel->getHeading());
			float heading_diff = adjustDeviation(fabs(gs.mBearing - heading));
			if(fabs(heading_diff) > 120)
			{
				found = 0;
			}
		}*/
				
		// calculate glide slope deviation
		float altitude = mModel->getAltitude();
		float angle = gs.getAngle(position,static_cast<float>(ft_to_m(altitude)));
		deviation = angle - gs.mAngle;

		// calculate distance (used by approach monitor)
		float beacon_distance_nm = position.get_distance_nm(gs.mLatLon);
		distance = (float) nm_to_m(beacon_distance_nm);
	}

	mModel->setGS_Found(radio,found);
	mModel->setGS_Deviation(radio,deviation);
	mModel->setGS_Distance(radio,distance);
}
