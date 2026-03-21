#pragma once
#include <Network.h>

namespace CIGI_IG_Interface_NS
{
	using SendRepositionMessage = struct 
	{
		double msl_altitude_ft; // Feet
		double latitude_deg; // Degrees
		double longitude_deg; // Degrees
		float psi; // Degrees
		float tas; // m/2
	};

	class RepositionBase
	{
		Network* m_network;
	public:
		RepositionBase(const Network_NS::OutAddress_sptr& outAddress, double altOffset);
		virtual ~RepositionBase();
	protected:
		double m_altOffset;
		void SendReposition() const;
		virtual double GetLat() const =0;
		virtual double GetLon() const =0;
		virtual double GetAltFt() const =0;
		virtual float GetPsi() const =0;
		virtual float GetTas() const =0;
	};
}
