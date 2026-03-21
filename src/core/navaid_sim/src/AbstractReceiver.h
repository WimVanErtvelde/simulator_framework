#pragma once
#ifndef AS_ABSTRACT_RECEIVER_H
#define AS_ABSTRACT_RECEIVER_H

#include "World.h"
#include "Model.h"
#include "LOSChecker.h"
//#include "Ice.h"

namespace AS
{
	class AbstractReceiver
	{
	public:
		AbstractReceiver(World* world,Model* model);
		void update();

		// Attach a LOSChecker so this receiver performs terrain LOS checks.
		// Call once after construction (see NavSimTask).
		// NDB receivers should NOT have a LOSChecker set (ground-wave propagation).
		void setLOSChecker(LOSChecker* los) { mLOS = los; }

	protected:
		virtual void updateRadio(int radio) = 0;
		float adjustHeading(float degrees);
		float adjustDeviation(float degrees);
		bool isLocalizer(int frequency);
		World*      mWorld;
		Model*      mModel;
		LOSChecker* mLOS = nullptr;   // nullptr → skip LOS check
	};
}

#endif