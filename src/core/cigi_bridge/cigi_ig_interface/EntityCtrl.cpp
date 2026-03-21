#include "EntityCtrl.h"
#include <CigiEntityCtrlV3_3.h>

using namespace CIGI_IG_Interface_NS;

EntityCtrl::EntityCtrl(IEntityCtrlProcessor* entityCtrlProcessor) : m_processor(entityCtrlProcessor), m_oldEntityType(0)
{
}

EntityCtrl::~EntityCtrl() = default;

void EntityCtrl::OnPacketReceived(CigiBasePacket* packet)
{
	const CigiEntityCtrlV3_3* entityCtrlPacket = static_cast<CigiEntityCtrlV3_3*>(packet);

	if(entityCtrlPacket->GetEntityID() != 0)
	{
		return;
	}

	PositionAndOrientation pov;
	pov.lat = entityCtrlPacket->GetLat();
	pov.lon = entityCtrlPacket->GetLon();
	pov.alt = entityCtrlPacket->GetAlt();

	pov.bank = -static_cast<double>(entityCtrlPacket->GetRoll());
	pov.pitch = -static_cast<double>(entityCtrlPacket->GetPitch());
	pov.heading = static_cast<double>(entityCtrlPacket->GetYaw());

	const Cigi_uint16 entityType = entityCtrlPacket->GetEntityType();

	if(entityType != m_oldEntityType)
	{
		m_processor->ChangeEntityType(entityType);
		m_oldEntityType = entityType;
	}

	m_processor->SetPositionAndOrientation(pov);
}
