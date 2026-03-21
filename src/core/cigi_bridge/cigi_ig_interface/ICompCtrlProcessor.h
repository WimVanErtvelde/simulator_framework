#pragma once

#include "CompCtrlItem.h"

namespace CIGI_IG_Interface_NS
{
	class ICompCtrlProcessor
	{
	public:
		virtual ~ICompCtrlProcessor() = default;
		virtual void ProcessCompCtrlItem(const CompCtrlItem& item) const = 0;
	};
}
