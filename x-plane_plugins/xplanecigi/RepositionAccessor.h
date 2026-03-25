#pragma once
#include "IRepositionAccessor.h"
#include "XPLMDataRefAccessor.h"

class RepositionAccessor final : public CIGI_IG_Interface_NS::IRepositionAccessor
{
	XPLMDataRefAccessor_d m_lat;
	XPLMDataRefAccessor_d m_lon;
	XPLMDataRefAccessor_d m_alt;

	XPLMDataRefAccessor_f m_phi;
	XPLMDataRefAccessor_f m_theta;
	XPLMDataRefAccessor_f m_psi;
	XPLMDataRefAccessor_f m_tas;

public:
	explicit RepositionAccessor()
		: m_lat(XPLMDataRefAccessor_d("sim/flightmodel/position/latitude")),
		m_lon(XPLMDataRefAccessor_d("sim/flightmodel/position/longitude")),
		m_alt(XPLMDataRefAccessor_d("sim/flightmodel/position/elevation")),
		m_phi(XPLMDataRefAccessor_f("sim/flightmodel/position/phi")),
		m_theta(XPLMDataRefAccessor_f("sim/flightmodel/position/theta")),
		m_psi(XPLMDataRefAccessor_f("sim/flightmodel/position/psi")),
		m_tas(XPLMDataRefAccessor_f("sim/flightmodel/position/groundspeed"))
	{
	}

	~RepositionAccessor() override = default;

	[[nodiscard]] double GetLat() const override { return m_lat(); }
	[[nodiscard]] double GetLon() const override { return m_lon(); }
	[[nodiscard]] double GetAlt() const override { return m_lat(); }
	[[nodiscard]] float GetPhi() const override { return m_phi(); }
	[[nodiscard]] float GetTheta() const override { return m_theta(); }
	[[nodiscard]] float GetPsi() const override { return m_psi(); }
	[[nodiscard]] float GetTas() const override { return m_tas(); }
};
