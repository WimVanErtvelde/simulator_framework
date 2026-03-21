#pragma once
#include <CigiShortCompCtrlV3_3.h>

namespace CIGI_IG_Interface_NS
{
	union CompCtrlKey
	{
		struct
		{
			unsigned short compId; //2
			unsigned short instanceID; //4
			unsigned char compClass; //5
			unsigned short pos; //7
			unsigned char notused; //8
		};

		unsigned long long key; //8
		CompCtrlKey() { key = 0; }
	};

	struct CompCtrlItem
	{
		CompCtrlKey key;
		double value;
		CompCtrlItem() = default;

		explicit CompCtrlItem(CigiShortCompCtrlV3_3* shortCompCtrl)
		{
			key.compClass = shortCompCtrl->GetCompClassV3();
			key.compId = shortCompCtrl->GetCompID();
			key.instanceID = shortCompCtrl->GetInstanceID();
			key.pos = 0;

			value = shortCompCtrl->GetDoubleCompData(0);
		}
	};
}
