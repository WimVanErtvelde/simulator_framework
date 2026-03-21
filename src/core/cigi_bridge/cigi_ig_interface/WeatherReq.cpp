#include "WeatherReq.h"

#include "CigiEnvCondReqV3.h"
#include "CigiWeatherCondRespV3.h"

using namespace CIGI_IG_Interface_NS;

CigiOutgoingMsg &operator<<(CigiOutgoingMsg& msg, const WeatherReq& weatherReq)
{
	return weatherReq.Put(msg);
}

WeatherReq::WeatherReq(IWeatherCondProcessor* weatherCondProcessor): m_processor(weatherCondProcessor),
	m_weatherCondRequest(nullptr)
{
}

void WeatherReq::OnPacketReceived(CigiBasePacket* packet)
{
	const CigiEnvCondReqV3* envCondReq = static_cast<CigiEnvCondReqV3*>(packet);

	if(envCondReq->GetReqType() == CigiBaseEnvCondReq::Weather)
	{
		m_weatherCondRequest = envCondReq;
	}
}

CigiOutgoingMsg &WeatherReq::Put(CigiOutgoingMsg& msg) const
{
	if(m_weatherCondRequest != nullptr)
	{
		CigiWeatherCondRespV3 weatherCondResponse;
		Weather_t weather;
		m_processor->GetWeather(m_weatherCondRequest->GetLat(), m_weatherCondRequest->GetLon(), m_weatherCondRequest->GetAlt(), weather);

		weatherCondResponse.SetRequestID(m_weatherCondRequest->GetReqID());
		weatherCondResponse.SetHorizWindSp(weather.horizWindSp);
		weatherCondResponse.SetWindDir(weather.windDir);
		weatherCondResponse.SetVertWindSp(weather.vertWindSp);
		weatherCondResponse.SetAirTemp(weather.airTemp);
		weatherCondResponse.SetBaroPress(weather.baroPress);

		msg << weatherCondResponse;

		m_weatherCondRequest = nullptr;
	}

	return msg;
}
