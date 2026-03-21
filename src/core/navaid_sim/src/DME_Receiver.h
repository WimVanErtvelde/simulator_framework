#pragma once

#include "AbstractReceiver.h"

namespace AS
{
	class DME_Receiver : public AbstractReceiver
	{
	public:
		DME_Receiver(World* world, Model* model);
	private:
		void updateRadio(int radio);
		void updateRadioDME(int radio);

		uint64_t ticksOnLastUpdate = 0;
		const uint64_t updateInterval = 2000;
		int freqOnLastUpdate = 0;
	};
}
