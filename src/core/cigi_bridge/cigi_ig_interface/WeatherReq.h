#pragma once
#include <CigiBaseEventProcessor.h>
#include <CigiOutgoingMsg.h>
#include "CigiEnvCondReqV3.h"
#include "IWeatherCondProcessor.h"

namespace CIGI_IG_Interface_NS
{
	class WeatherReq final : public CigiBaseEventProcessor
	{
		IWeatherCondProcessor* m_processor;
		mutable const CigiEnvCondReqV3* m_weatherCondRequest;
	public:
		explicit WeatherReq(IWeatherCondProcessor* weatherCondProcessor);
		~WeatherReq() override = default;
		void OnPacketReceived(CigiBasePacket* packet) override;
		CigiOutgoingMsg &Put(CigiOutgoingMsg& msg) const;
	};
}

CigiOutgoingMsg &operator <<(CigiOutgoingMsg& msg, const CIGI_IG_Interface_NS::WeatherReq& weatherReq);
