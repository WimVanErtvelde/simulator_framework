#include "ShortCompCtrl.h"
#include <CigiShortCompCtrlV3_3.h>

#include "CompCtrlItem.h"

using namespace CIGI_IG_Interface_NS;

ShortCompCtrl::ShortCompCtrl(ICompCtrlProcessor* compCtrlProcessor)
	: m_processor(compCtrlProcessor)
{
}

// ================================================
// OnPacketReceived
// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
void ShortCompCtrl::OnPacketReceived(CigiBasePacket* packet)
{
	m_processor->ProcessCompCtrlItem(CompCtrlItem(static_cast<CigiShortCompCtrlV3_3*>(packet)));
}
