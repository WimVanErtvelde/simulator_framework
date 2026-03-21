#pragma once
#ifndef AS_VOR_RECEIVER_H
#define AS_VOR_RECEIVER_H

#include "AbstractReceiver.h"

namespace AS
{
	class VOR_Receiver : public AbstractReceiver
	{
	public:
		VOR_Receiver(World* world,Model* model);
	private:
		void updateRadio(int radio);
		void updateRadioVOR(int radio);
	};
}

#endif