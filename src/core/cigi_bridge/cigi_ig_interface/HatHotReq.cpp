#include "HatHotReq.h"

#include "CigiHatHotRespV3_2.h"

using namespace CIGI_IG_Interface_NS;

CigiOutgoingMsg &operator <<(CigiOutgoingMsg& msg, const HatHotReq& hatHotReq)
{
	return hatHotReq.Put(msg);
}

HatHotReq::HatHotReq(IHotHatProcessor* processor, IGCtrl& igCtrl) : m_processor(processor),
	m_hatHotRequests(new HatHotResponse()), m_igCtrl(igCtrl)
{
}

HatHotReq::~HatHotReq()
{
	delete m_hatHotRequests;
}

void HatHotReq::OnPacketReceived(CigiBasePacket* packet)
{
	CigiHatHotRespV3_2* cigiHatHotRespV32 = GetCigiHatHotResp(static_cast<CigiHatHotReqV3_2*>(packet));

	if(cigiHatHotRespV32 != nullptr)
	{
		m_hatHotRequests->push(cigiHatHotRespV32);
	}
}

CigiHatHotRespV3_2 *HatHotReq::GetCigiHatHotResp(const CigiHatHotReqV3_2* hatHotRequest) const
{
	CigiHatHotRespV3_2* response = new CigiHatHotRespV3_2();

	response->SetHostFrame(static_cast<Cigi_uint8>(m_igCtrl.GetLastRcvdIGFrame()));
	response->SetHatHotID(hatHotRequest->GetHatHotID());

	if(hatHotRequest->GetEntityID() != 0)
	{
		return nullptr;
	}

	switch(hatHotRequest->GetReqType())
	{
	case CigiBaseHatHotReq::HOT:
		{
			double hot;
			bool valid;
			m_processor->GetHot(hatHotRequest->GetLat(), hatHotRequest->GetLon(), hatHotRequest->GetAlt(), hot, valid);
			response->SetHot(hot);
			response->SetValid(valid);
			return response;
		}
	case CigiBaseHatHotReq::HAT:
		{
			double hat;
			bool valid;
			m_processor->GetHat(hatHotRequest->GetLat(), hatHotRequest->GetLon(), hatHotRequest->GetAlt(), hat, valid);
			response->SetHat(hat);
			response->SetValid(valid);
			return response;
		}
	case CigiBaseHatHotReq::Extended:
		{
			response->SetHat(0);
			response->SetValid(false);
			return response;
		}
	}

	return nullptr;
}

CigiOutgoingMsg &HatHotReq::Put(CigiOutgoingMsg& msg) const
{
	while(!m_hatHotRequests->empty())
	{
		CigiHatHotRespV3_2* r = m_hatHotRequests->front();
		msg << *r;
		delete r;
		m_hatHotRequests->pop();
	}

	return msg;
}
