#pragma once

#include <CigiBaseEventProcessor.h>

#include "IEntityCtrlProcessor.h"

namespace CIGI_IG_Interface_NS
{
	class EntityCtrl final : public CigiBaseEventProcessor
	{
		IEntityCtrlProcessor* m_processor;
		uint16_t m_oldEntityType;
	public:
		explicit EntityCtrl(IEntityCtrlProcessor* entityCtrlProcessor);
		~EntityCtrl() override;

		void OnPacketReceived(CigiBasePacket* packet) override;
	};
}
