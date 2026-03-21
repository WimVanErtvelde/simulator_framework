#include "RepositionBase.h"

using namespace CIGI_IG_Interface_NS;
using namespace Network_NS;

RepositionBase::RepositionBase(const OutAddress_sptr& outAddress, const double altOffset)
	: m_network(new Network()), m_altOffset(altOffset)
{
	m_network->openSocket(outAddress);
}

RepositionBase::~RepositionBase()
{
	delete m_network;
}

void RepositionBase::SendReposition() const
{
	SendRepositionMessage message;
	message.msl_altitude_ft = GetAltFt();
	message.latitude_deg = GetLat();
	message.longitude_deg = GetLon();

	message.psi = GetPsi();
	message.tas = GetTas();

	m_network->send(reinterpret_cast<unsigned char*>(&message), sizeof(SendRepositionMessage));
}
