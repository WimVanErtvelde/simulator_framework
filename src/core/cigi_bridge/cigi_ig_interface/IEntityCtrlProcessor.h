#pragma once

#include "CigiTypes.h"
#include "PositionAndOrientation.h"

namespace CIGI_IG_Interface_NS
{
	class IEntityCtrlProcessor
	{
	public:
		virtual ~IEntityCtrlProcessor() = default;
		virtual void SetPositionAndOrientation(const PositionAndOrientation& pov) =0;
		virtual void ChangeEntityType(Cigi_uint16 entityType) =0;
	};
}
