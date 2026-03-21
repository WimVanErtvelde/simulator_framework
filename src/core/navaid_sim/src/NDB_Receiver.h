#ifndef AS_NDB_RECEIVER_H
#define AS_NDB_RECEIVER_H

#include "AbstractReceiver.h"

namespace AS
{
	class NDB_Receiver : public AbstractReceiver
	{
	public:
		NDB_Receiver(World* world,Model* model);
	private:
		void updateRadio(int radio);
	};
}

#endif