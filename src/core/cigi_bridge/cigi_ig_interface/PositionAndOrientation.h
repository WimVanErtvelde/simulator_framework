#pragma once

#pragma pack(push,1)
namespace CIGI_IG_Interface_NS
{
	struct PositionAndOrientation
	{
		double lat;
		double lon;
		double alt;
		double pitch;
		double bank;
		double heading;
	};
}
#pragma pack(pop)
