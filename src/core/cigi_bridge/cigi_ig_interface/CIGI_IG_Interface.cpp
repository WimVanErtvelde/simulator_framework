#include <CigiException.h>
#include <CigiShortCompCtrlV3_3.h>
#include <stdexcept>
#include <CigiBaseEnvCondReq.h>
#include <CigiBaseHatHotReq.h>
#include "IGCtrl.h"

#include "CIGI_IG_Interface.h"

using namespace CIGI_IG_Interface_NS;
using namespace Network_NS;

enum
{
	recv_buffer_size = 32768
};

ICIGI_IG_Interface *CIGI_IG_Interface_NS::CreateCIGI_IG_Interface(const InAddress_sptr& igAddress,
																  const OutAddress_sptr& hostAddress,
																  IEntityCtrlProcessor* entityCtrlProcessor,
																  IHotHatProcessor* hotHatProcessor,
																  IWeatherCondProcessor* weatherCondProcessor,
																  ICompCtrlProcessor* compCtrlProcessor)
{
	return new CIGI_IG_Interface(igAddress, hostAddress, entityCtrlProcessor, hotHatProcessor, weatherCondProcessor, compCtrlProcessor);
}

CIGI_IG_Interface::CIGI_IG_Interface(const InAddress_sptr& igAddress,
									 const OutAddress_sptr& hostAddress,
									 IEntityCtrlProcessor* entityCtrlProcessor,
									 IHotHatProcessor* hotHatProcessor,
									 IWeatherCondProcessor* weatherCondProcessor,
									 ICompCtrlProcessor* compCtrlProcessor)
	: m_network(new Network()),
	m_entityCtrl(entityCtrlProcessor),
	m_hotHatReq(hotHatProcessor, m_igCtrl),
	m_weatherReq(weatherCondProcessor),
	m_shortCompCtrl(compCtrlProcessor),
	m_outFrame(-1),
	m_send(hostAddress->Valid)
{
	const bool netStatus = m_send ? m_network->openSocket(igAddress, hostAddress) : m_network->openSocket(hostAddress);

	if(!netStatus)
	{
		throw runtime_error("could not connect to CIGI IG server");
	}

	m_hostSn = new CigiIGSession(1, 32768, 2, 32768);

	m_hostSn->SetCigiVersion(3, 3);
	m_hostSn->SetSynchronous(true);

	CigiIncomingMsg& incomingMsg = m_hostSn->GetIncomingMsgMgr();

	incomingMsg.SetReaderCigiVersion(3, 3);
	incomingMsg.UsingIteration(false);

	// set up a default handler for unhandled packets
	incomingMsg.RegisterEventProcessor(0, &m_default);
	incomingMsg.RegisterEventProcessor(CIGI_IG_CTRL_PACKET_ID_V3_3, &m_igCtrl);
	incomingMsg.RegisterEventProcessor(CIGI_ENTITY_CTRL_PACKET_ID_V3_3, &m_entityCtrl);
	incomingMsg.RegisterEventProcessor(CIGI_HAT_HOT_REQ_PACKET_ID_V3_2, &m_hotHatReq);
	incomingMsg.RegisterEventProcessor(CIGI_ENV_COND_REQ_PACKET_ID_V3, &m_weatherReq);
	incomingMsg.RegisterEventProcessor(CIGI_SHORT_COMP_CTRL_PACKET_ID_V3_3, &m_shortCompCtrl);

	m_sof.SetDatabaseID(1);
	m_sof.SetIGStatus(0);
	m_sof.SetIGMode(CigiBaseSOF::Operate);
	m_sof.SetTimeStampValid(false);
	m_sof.SetEarthRefModel(CigiBaseSOF::WGS84);
	m_sof.SetTimeStamp(0);
	m_sof.SetFrameCntr(0);

	if(m_send)
	{
		CigiOutgoingMsg& outgoingMsg = m_hostSn->GetOutgoingMsgMgr();
		outgoingMsg.BeginMsg();
	}
}

CIGI_IG_Interface::~CIGI_IG_Interface()
{
	delete m_hostSn;
	delete m_network;
}

void CIGI_IG_Interface::ReceiveMessage() const
{
	CigiIncomingMsg& incomingMsg = m_hostSn->GetIncomingMsgMgr();

	int cigiInSz;
	Cigi_uint32 inFrame = 0;

	do
	{
		unsigned char cInBuf[recv_buffer_size];
		cigiInSz = m_network->recv(cInBuf, recv_buffer_size);

		/* process incoming CIGI message - this could be long */
		if(cigiInSz > 0)
		{
			incomingMsg.ProcessIncomingMsg(cInBuf, cigiInSz);
			inFrame = m_igCtrl.GetLastRcvdIGFrame();
		}

		Sleep(0);
	}
	while(cigiInSz >= 0 && inFrame < m_outFrame);
}

void CIGI_IG_Interface::SendMessage()
{
	CigiOutgoingMsg& outgoingMsg = m_hostSn->GetOutgoingMsgMgr();

	// load the IG Control
	outgoingMsg << m_sof << m_hotHatReq << m_weatherReq;

	// Do packaging here to 
	// Package msg
	unsigned char* pCigiOutBuf;
	int cigiOutSz;

	outgoingMsg.PackageMsg(&pCigiOutBuf, cigiOutSz);

	// Update Frame IDs
	m_outFrame = outgoingMsg.GetFrameCnt();
	outgoingMsg.UpdateSOF(pCigiOutBuf);

	// send SOF message
	m_network->send(pCigiOutBuf, cigiOutSz);
	outgoingMsg.FreeMsg(); // Frees the buffer containing the message that was just sent

	Sleep(0);
}

void CIGI_IG_Interface::SendAndReceiveMessage()
{
	ReceiveMessage();

	if(m_send)
	{
		SendMessage();
	}
}
