#pragma once

#include <Network.h>
#include <CigiSOFV3_2.h>
#include <CigiIGSession.h>
#include "DefaultProc.h"
#include "IGCtrl.h"
#include "EntityCtrl.h"
#include "HatHotReq.h"
#include "WeatherReq.h"
#include "ShortCompCtrl.h"
#include "ICIGI_IG_Interface.h"

#undef SendMessage
namespace CIGI_IG_Interface_NS
{
	class CIGI_IG_Interface final : public ICIGI_IG_Interface
	{
		CigiIGSession* m_hostSn;
		Network* m_network;
		CigiSOFV3_2 m_sof;

		DefaultProc m_default;
		IGCtrl m_igCtrl;
		EntityCtrl m_entityCtrl;
		HatHotReq m_hotHatReq;
		WeatherReq m_weatherReq;
		ShortCompCtrl m_shortCompCtrl;

		uint32_t m_outFrame;
		bool m_send;

	public:
		CIGI_IG_Interface(const Network_NS::InAddress_sptr& igAddress,
						  const Network_NS::OutAddress_sptr& hostAddress,
						  IEntityCtrlProcessor* entityCtrlProcessor,
						  IHotHatProcessor* hotHatProcessor,
						  IWeatherCondProcessor* weatherCondProcessor,
						  ICompCtrlProcessor* compCtrlProcessor);
		~CIGI_IG_Interface() override;
		void ReceiveMessage() const;
		void SendMessage();
		void SendAndReceiveMessage() override;
	};
}
