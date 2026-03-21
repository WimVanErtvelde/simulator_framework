#pragma once

#include <CigiBaseEventProcessor.h>
#include <CigiIGCtrlV3_3.h>

namespace CIGI_IG_Interface_NS
{
	class IGCtrl final : public CigiBaseEventProcessor
	{
	public:
		IGCtrl();
		~IGCtrl() override;

		void OnPacketReceived(CigiBasePacket* packet) override;
		Cigi_uint32 GetLastRcvdIGFrame() const { return m_packet->GetLastRcvdIGFrame(); }
	protected:
		CigiIGCtrlV3_3* m_packet;
	};
}
