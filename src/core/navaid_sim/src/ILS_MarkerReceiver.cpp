#include "ILS_MarkerReceiver.h"

AS::ILS_MarkerReceiver::ILS_MarkerReceiver(AS::World* world,Model* model) : AS::AbstractReceiver(world,model)
{
}

void AS::ILS_MarkerReceiver::updateOnce()
{
	// get aircraft and radio parameters
	LatLon position(mModel->getLat(),mModel->getLon());
	// float altitude = mModel->getAltitude();  // unused — marker is distance-only

	std::vector<AS::ILS_Marker>* mMarkers = mWorld->getILS_Markers();

	int inner = 0;
	int outer = 0;
	int middle = 0;
	
	// find marker within range
	for(unsigned int i = 0; i < mMarkers->size(); i++)
	{
		AS::ILS_Marker marker = (*mMarkers)[i];
		float distance_nm = position.get_distance_nm(marker.mLatLon);

		float distance = (float) nm_to_m(distance_nm);

		if(marker.mType.compare("OM") == 0 && distance < 1000)
		{
			outer = 1;
		}
		
		if(marker.mType.compare("MM") == 0 && distance < 500)
		{
			middle = 1;
		}
		
		if(marker.mType.compare("IM") == 0 && distance < 100)
		{
			inner = 1;
		}
	}

	mModel->setInnerMarker(inner);
	mModel->setOuterMarker(outer);
	mModel->setMiddleMarker(middle);
}

// we're not doing any per-radio operations, since all the marker reception is handled once for the whole aircraft
void AS::ILS_MarkerReceiver::updateRadio(int /*radio*/)
{
}
	