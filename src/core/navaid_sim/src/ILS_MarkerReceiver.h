#pragma once
#ifndef AS_ILS_MARKER_RECEIVER_H
#define AS_ILS_MARKER_RECEIVER_H

#include "AbstractReceiver.h"
#include "ILS-Marker.h"

namespace AS
{
	class ILS_MarkerReceiver : public AbstractReceiver
	{
	public:
		ILS_MarkerReceiver(World* world,Model* model);
		void updateOnce();
	private:
		void updateRadio(int radio);
	};
}

#endif