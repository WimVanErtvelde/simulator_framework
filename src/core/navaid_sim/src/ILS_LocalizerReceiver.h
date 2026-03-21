#pragma once
#ifndef AS_ILS_LOCALIZER_RECEIVER_H
#define AS_ILS_LOCALIZER_RECEIVER_H

#include "AbstractReceiver.h"

namespace AS
{
	class ILS_LocalizerReceiver : public AbstractReceiver
	{
	public:
		ILS_LocalizerReceiver(World* world,Model* model);
	private:
		void updateRadio(int radio);
		void updateRadioLoc(int radio);
		float getDeviation(LatLon position,ILS_LOC loc);
	};
}

#endif