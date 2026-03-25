#include "XPlaneData.h"
#include <XPLMGraphics.h>
#include <XPLMPlanes.h>

#include "DataRefFactory.h"
#include <cmath>

#undef max
#define RAD_TO_DEG(radians) ((radians) * (180.0f / 3.14159265358979323846f))
#define IN_HG_TO_MILLBAR(inhg) ((inhg) / 0.029530f)

using namespace CIGI_IG_Interface_NS;

std::unique_ptr<std::string> XPlaneData::s_system_path = nullptr;

const std::string &XPlaneData::SystemPath()
{
	if(s_system_path == nullptr)
	{
		char systemPath[MAX_PATH];
		XPLMGetSystemPath(systemPath);
		s_system_path = std::make_unique<std::string>(systemPath);
	}

	return *s_system_path;
}

XPlaneData::XPlaneData(const XPlaneConfig& config)
	: m_config(config)
{
	m_localX = DataRefFactory::GetDataRef("sim/flightmodel/position/local_x");
	m_localY = DataRefFactory::GetDataRef("sim/flightmodel/position/local_y");
	m_localZ = DataRefFactory::GetDataRef("sim/flightmodel/position/local_z");

	m_phi = DataRefFactory::GetDataRef("sim/flightmodel/position/phi");
	m_theta = DataRefFactory::GetDataRef("sim/flightmodel/position/theta");
	m_psi = DataRefFactory::GetDataRef("sim/flightmodel/position/psi");

	m_probe = XPLMCreateProbe(xplm_ProbeY);

	m_windDirection = DataRefFactory::GetDataRef("sim/weather/wind_direction_degt");
	m_windSpeed = DataRefFactory::GetDataRef("sim/weather/wind_speed_kt");

	m_airTemp_c = DataRefFactory::GetDataRef("sim/weather/temperature_ambient_c");
	m_baroPress_inhg = DataRefFactory::GetDataRef("sim/weather/barometer_current_inhg");

	m_paused = DataRefFactory::GetDataRef("sim/time/paused");
	m_pauseToggleCommand = XPLMFindCommand("sim/operation/pause_toggle");
}

XPlaneData::~XPlaneData()
{
	XPLMDestroyProbe(m_probe);
}

void XPlaneData::SetPositionAndOrientation(const PositionAndOrientation& pvo)
{
	double x,y,z;

	XPLMWorldToLocal(pvo.lat, pvo.lon, pvo.alt, &x, &y, &z);

	XPLMSetDatad(m_localX, x);
	XPLMSetDatad(m_localY, y);
	XPLMSetDatad(m_localZ, z);

	const float phi = static_cast<float>(-pvo.bank);
	const float theta = static_cast<float>(-pvo.pitch);
	const float psi = static_cast<float>(pvo.heading);

	XPLMSetDataf(m_phi, phi);
	XPLMSetDataf(m_theta, theta);
	XPLMSetDataf(m_psi, psi);
}

void XPlaneData::ChangeEntityType(const std::string& aircraftToLoad)
{
	std::stringstream pathStream;
	pathStream << SystemPath() << "Aircraft\\" << aircraftToLoad;
	XPLMSetUsersAircraft(pathStream.str().c_str());
	SetConstants();
	CallCommands();
}

void XPlaneData::GetHat(const Position& position, double& hat, bool& valid) const
{
	double hot;
	GetHot(position, hot, valid);
	hat = position.alt - hot;
}

void XPlaneData::GetHot(const Position& position, double& hot, bool& valid) const
{
	GetHotInternal(position, hot, valid, nullptr);
}

void XPlaneData::GetXHot(const Position& position, double& hot, bool& valid, ExtendedInfo& extendedInfo) const
{
	GetHotInternal(position, hot, valid, &extendedInfo);
}

void XPlaneData::GetHotInternal(const Position& position, double& hot, bool& valid, ExtendedInfo* extendedInfo) const
{
	try
	{
		double x;
		double y;
		double z;

		XPLMWorldToLocal(position.lat, position.lon, position.alt, &x, &y, &z);

		XPLMProbeInfo_t outInfo;
		outInfo.structSize = sizeof outInfo;

		if(XPLMProbeTerrainXYZ(m_probe, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), &outInfo) == xplm_ProbeHitTerrain)
		{
			double outLat;
			double outLon;
			double outAlt;
			XPLMLocalToWorld(outInfo.locationX, outInfo.locationY, outInfo.locationZ, &outLat, &outLon, &outAlt);
			hot = outAlt;
			valid = true;

			if(extendedInfo != nullptr)
			{
				extendedInfo->material = std::numeric_limits<unsigned short>::max();
				extendedInfo->normalAz = RAD_TO_DEG(std::atan2(outInfo.normalX, outInfo.normalY));
				extendedInfo->normalEL = RAD_TO_DEG(std::asin(outInfo.normalZ));
			}
		}
		else
		{
			hot = 0;
			valid = false;
		}
	}
	catch(std::exception& e)
	{
		XPLMDebugString((std::string("GetHot ") + std::string(e.what())).c_str());
		throw;
	}
}

void XPlaneData::GetWeather(const double getLat, const double getLon, const double getAlt, Weather_t& weather)
{
	try
	{
		weather.horizWindSp = XPLMGetDataf(m_windSpeed);
		weather.windDir = XPLMGetDataf(m_windDirection);
		weather.vertWindSp = 0.0f;

		if(m_config.UseTempPressure())
		{
			weather.airTemp = XPLMGetDataf(m_airTemp_c);
			weather.baroPress = IN_HG_TO_MILLBAR(XPLMGetDataf(m_baroPress_inhg));
		}
		else
		{
			weather.airTemp = 0.0;
			weather.baroPress = 0.0;
		}
	}
	catch(std::exception& e)
	{
		XPLMDebugString((std::string("GetWeather ") + std::string(e.what())).c_str());
		throw;
	}
}

void XPlaneData::CallCommands() const
{
	for(const auto i : m_config.CommandList())
	{
		XPLMCommandOnce(i);
	}
}

void XPlaneData::SetConstants() const
{
	for(const auto& data : m_config.ConstantsList())
	{
		data->SetValue();
	}
}

void XPlaneData::TogglePause()
{
	XPLMCommandOnce(m_pauseToggleCommand);
}

bool XPlaneData::IG_IsPaused()
{
	return XPLMGetDatai(m_paused) == 1;
}
