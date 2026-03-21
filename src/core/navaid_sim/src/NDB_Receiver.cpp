#include "NDB_Receiver.h"
#include <math.h>

AS::NDB_Receiver::NDB_Receiver(AS::World* world,Model* model) : AS::AbstractReceiver(world,model)
{
}

void AS::NDB_Receiver::updateRadio(int radio)
{
	if (radio != 1) return;
	// get aircraft and radio parameters
	LatLon position(mModel->getLat(),mModel->getLon());
	int frequency = mModel->getADF_Frequency();

	// get all NDB's with given frequency within range
	std::vector<NDB> candidates = mWorld->getNDB(frequency,position);

	// results
	int found = 0;
	float distance = 0;
	float bearing = 0;
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

		// select the first NDB that matches
		NDB ndb = candidates[i];

		// calculate bearing
		bearing = adjustHeading(position.get_course_deg(ndb.mLatLon) - mModel->getHeading());

		// check zone of confusion
		distance = (float)nm_to_m(position.get_distance_nm(ndb.mLatLon));
		
		// check whether we still need to look for another NDB
		if(found > 0)
		{
			// copy name
			for(unsigned int i = 0; i < IDENT_SIZE && i < ndb.mIdent.size(); i++)
			{
				ident[i] = (int) ndb.mIdent[i];
			}

			// stop looking for other NDB's
			break;
		}
	}

	// send values to Ice
	mModel->setNDB_Found(radio,found);
	mModel->setNDB_Bearing(radio,bearing);
	mModel->setNDB_Distance(radio,distance);

	// send identifier (e.g. "CZI") to Ice
	for(int i = 0; i < IDENT_SIZE; i++)
	{
		mModel->setNDB_Ident(radio,i,ident[i]);
	}
}