#pragma once
#ifndef AS_ILS_GLIDESLOPE_RECEIVER_H
#define AS_ILS_GLIDESLOPE_RECEIVER_H

#include "AbstractReceiver.h"

namespace AS
{
	class ILS_GlideslopeReceiver : public AbstractReceiver
	{
	public:
		ILS_GlideslopeReceiver(World* world,Model* model);
	private:
		virtual void updateRadio(int radio);
		float getDeviation(LatLon position,ILS_GS gs);

	};
}

#endif