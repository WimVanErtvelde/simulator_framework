#pragma once

#include <CigiBaseEventProcessor.h>

#include "ICompCtrlProcessor.h"

namespace CIGI_IG_Interface_NS
{
	class ShortCompCtrl final : public CigiBaseEventProcessor
	{
		ICompCtrlProcessor* m_processor;

	public:
		explicit ShortCompCtrl(ICompCtrlProcessor* compCtrlProcessor);
		~ShortCompCtrl() override = default;
		void OnPacketReceived(CigiBasePacket* packet) override;
	};
}
