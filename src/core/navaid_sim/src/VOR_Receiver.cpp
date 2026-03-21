#include "VOR_Receiver.h"
#include <math.h>

AS::VOR_Receiver::VOR_Receiver(AS::World* world,Model* model) : AS::AbstractReceiver(world,model)
{
}

void AS::VOR_Receiver::updateRadio(int radio)
{
	// only update if we're not on a localizer frequency
	if(!isLocalizer(mModel->getFrequency(radio)))
	{
		updateRadioVOR(radio);
	}
}

void AS::VOR_Receiver::updateRadioVOR(int radio)
{
	// get aircraft and radio parameters
	LatLon position(mModel->getLat(),mModel->getLon());
	int frequency = mModel->getFrequency(radio);

	// get all VOR's with given frequency within range
	std::vector<VOR> candidates = mWorld->getVOR(frequency,position);

	// results
	int found = 0;
	float deviation = 0;
	int from = 0;
	float distance = 0;
	float bearing = 0;
	float centered_obs = 0;
	int ident[IDENT_SIZE];
	for(int i = 0; i < IDENT_SIZE;i++)
	{
		// clear initial ident
		ident[i] = 0;
	}

	// check all candidates VOR's for a suitable one
	for(unsigned int i = 0; i < candidates.size(); i++)
	{
		// we assume that the VOR is valid, otherwise this flag needs to be set later on
		found = 1;

		// select the first VOR that matches
		VOR vor = candidates[i];

		// calculate which radial we are on
		float radial = adjustHeading((float)vor.mLatLon.get_course_deg(position));

		// adjust for magnetic variation
		radial = adjustHeading(radial - vor.mVariation);
		centered_obs = floor(adjustHeading(radial + 180) + 0.5f);

		// calculate expected radial
		int obs = mModel->getOBS(radio);
		float expected_radial = adjustHeading(static_cast<float>(obs) + 180.0f);

		// calculated deviation (positive means we're to the right of the radial)
		deviation = radial - expected_radial;

		// determine to FROM/TO flag
		from = (fabs(deviation) > 90.0) ? 1 : 0;

		// adjust deviation to range
		if(deviation >= 90) deviation = 180 - deviation;
		if(deviation <= -90) deviation = -(180 + deviation);

		// calculate bearing
		bearing = adjustHeading(position.get_course_deg(vor.mLatLon));

		// check zone of confusion
		float height = mModel->getAltitude() - vor.mElevation;
		distance = (float)nm_to_m(position.get_distance_nm(vor.mLatLon));
		float verticalAngleRad = atan2(height,distance);
		float verticalAngleDeg = (float)fabs(rad_to_deg(verticalAngleRad));

		/*// check if station has DME
		if (!vor.mHasDME) distance = 0;

		// check if station has VOR
		if (!vor.mHasVOR) {
			deviation = 0;
			from = 0;
			bearing = 0;
		}*/

		// consider aircraft inside zone of confusion if within given vertical range (90 deg is straight above VOR)
		if(verticalAngleDeg >= 80.0f)
		{
				found = 0;
		}

		// terrain line-of-sight check (VHF — strict LOS required)
		// vor.mElevation is in feet (no ft_to_m in VOR constructor)
		if(found && mLOS)
		{
			if(!mLOS->hasLOS(position, mModel->getAltitude(), vor.mLatLon, vor.mElevation))
				found = 0;
		}

		// check whether we still need to look for another VOR
		if(found > 0)
		{
			// copy name
			for(unsigned int i = 0; i < IDENT_SIZE && i < vor.mIdent.size(); i++)
			{
				ident[i] = (int) vor.mIdent[i];
			}

			// stop looking for other VOR's
			break;
		}
		else
		{
			// reset values
			deviation = 0;
			from = 0;
			bearing = 0;
			distance = 0;
		}
	}

	// send values to Ice
	mModel->setVOR_Localizer(radio,0); // always 0 since we checked we're not on a localizer frequency
	mModel->setVOR_DDM(radio,0); // DDM only used for localizers
	mModel->setVOR_Found(radio,found);
	mModel->setVOR_Deviation(radio,deviation);
	mModel->setVOR_From(radio,from);
	mModel->setVOR_Bearing(radio,bearing);
	mModel->setVOR_Distance(radio,distance);
	mModel->setVOR_CenteredOBS(radio,(float)centered_obs);

	// send identifier (e.g. "CZI") to Ice
	for(int i = 0; i < IDENT_SIZE; i++)
	{
		mModel->setVOR_Ident(radio,i,ident[i]);
	}
}