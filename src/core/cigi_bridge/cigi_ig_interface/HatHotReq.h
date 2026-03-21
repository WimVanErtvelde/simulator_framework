#pragma once

#include <CigiBaseEventProcessor.h>
#include <CigiOutgoingMsg.h>
#include <queue>

#include "CigiHatHotReqV3_2.h"
#include "CigiHatHotRespV3_2.h"

#include "IGCtrl.h"
#include "IHotHatProcessor.h"

namespace CIGI_IG_Interface_NS
{
	typedef queue<CigiHatHotRespV3_2*> HatHotResponse;

	class HatHotReq final : public CigiBaseEventProcessor
	{
		IHotHatProcessor* m_processor;
		HatHotResponse* m_hatHotRequests;
		IGCtrl& m_igCtrl;
	public:
		explicit HatHotReq(IHotHatProcessor* processor, IGCtrl& igCtrl);
		~HatHotReq() override;

		void OnPacketReceived(CigiBasePacket* packet) override;
		CigiHatHotRespV3_2 *GetCigiHatHotResp(const CigiHatHotReqV3_2* hatHotRequest) const;
		CigiOutgoingMsg &Put(CigiOutgoingMsg& msg) const;
	};
}

CigiOutgoingMsg &operator <<(CigiOutgoingMsg& msg, const CIGI_IG_Interface_NS::HatHotReq& hatHotReq);
