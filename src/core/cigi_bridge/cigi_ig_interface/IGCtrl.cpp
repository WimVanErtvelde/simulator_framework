// XIGCtrl.cpp: Body of the XIGCtrl class.
//
//////////////////////////////////////////////////////////////////////

#include "IGCtrl.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
using namespace CIGI_IG_Interface_NS;

IGCtrl::IGCtrl() : m_packet(nullptr)
{
}

IGCtrl::~IGCtrl()= default;

void IGCtrl::OnPacketReceived(CigiBasePacket* packet)
{
	m_packet = static_cast<CigiIGCtrlV3_3*>(packet);
}
