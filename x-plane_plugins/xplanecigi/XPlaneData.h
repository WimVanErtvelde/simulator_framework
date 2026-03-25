#pragma once

#include "IEntityCtrlProcessor.h"
#include "IHotHatProcessor.h"
#include "IWeatherCondProcessor.h"
#include "XPlaneConfig.h"

#include <XPLMDataAccess.h>
#include <XPLMScenery.h>
#include "IPause.h"
#include "RepositionAccessor.h"

class XPlaneData final : public CIGI_IG_Interface_NS::IHotHatProcessor,
						 public CIGI_IG_Interface_NS::IWeatherCondProcessor,
						 public CIGI_IG_Interface_NS::IEntityCtrlProcessor,
						 public CIGI_IG_Interface_NS::IPause
{
	static std::unique_ptr<std::string> s_system_path;

	const XPlaneConfig& m_config;

	XPLMDataRef m_localX = nullptr;
	XPLMDataRef m_localY = nullptr;
	XPLMDataRef m_localZ = nullptr;

	XPLMDataRef m_phi = nullptr;
	XPLMDataRef m_theta = nullptr;
	XPLMDataRef m_psi = nullptr;

	XPLMProbeRef m_probe = nullptr;

	XPLMDataRef m_windDirection = nullptr;
	XPLMDataRef m_windSpeed = nullptr;

	XPLMDataRef m_airTemp_c = nullptr;
	XPLMDataRef m_baroPress_inhg = nullptr;

	XPLMDataRef m_paused;
	XPLMCommandRef m_pauseToggleCommand;

	RepositionAccessor m_repositionAccessor;

public:
	static const std::string &SystemPath();

	explicit XPlaneData(const XPlaneConfig& config);
	~XPlaneData() override;

	void SetPositionAndOrientation(const CIGI_IG_Interface_NS::PositionAndOrientation& pvo) override;

	void ChangeEntityType(const std::string& aircraftToLoad) override;

	void GetHat(const CIGI_IG_Interface_NS::Position& position, double& hat, bool& valid) const override;
	void GetHot(const CIGI_IG_Interface_NS::Position& position, double& hot, bool& valid) const override;
	void GetXHot(const CIGI_IG_Interface_NS::Position& position, double& hot, bool& valid, CIGI_IG_Interface_NS::ExtendedInfo& extendedInfo) const override;

	void GetWeather(double getLat, double getLon, double getAlt, CIGI_IG_Interface_NS::Weather_t& weather) override;

	void SetConstants() const;

	RepositionAccessor &RepositionAccessor() { return m_repositionAccessor; }

	void TogglePause() override;
	bool IG_IsPaused() override;

	__declspec( property( get = GetPauseToggleCommand) ) XPLMCommandRef PauseToggleCommand;
	XPLMCommandRef GetPauseToggleCommand() const { return m_pauseToggleCommand; }

private:
	void CallCommands() const;
	void GetHotInternal(const CIGI_IG_Interface_NS::Position& position, double& hot, bool& valid, CIGI_IG_Interface_NS::ExtendedInfo* extendedInfo) const;
};
