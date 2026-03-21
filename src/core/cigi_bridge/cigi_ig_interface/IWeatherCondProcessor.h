#pragma once
namespace CIGI_IG_Interface_NS
{
	struct Weather_t
	{
		float horizWindSp; // m/s
		float windDir; // deg
		float vertWindSp; // m/s
		float airTemp; // °C
		float baroPress; //millibars
	};

	class IWeatherCondProcessor
	{
	public:
		virtual void GetWeather(double getLat, double getLon, double getAlt, Weather_t& weather) =0;
		virtual ~IWeatherCondProcessor() = default;
	};
}
