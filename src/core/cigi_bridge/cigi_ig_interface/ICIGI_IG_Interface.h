#pragma once

#include "IHotHatProcessor.h"
#include "ICompCtrlProcessor.h"
#include "IEntityCtrlProcessor.h"
#include "IWeatherCondProcessor.h"

#if !defined(_EXPORT) && (defined(_WIN64) || defined(WIN32))
#if !defined(_DLL)
			#error "must used /MD or /MDd"
#endif

#ifdef _WIN64
#if _DEBUG
	#pragma comment(lib, "CIGI_IG_InterfaceD_x64.lib")
#else
				#pragma comment(lib, "CIGI_IG_Interface_x64.lib")
#endif
#elif _WIN32
#if _DEBUG
				#pragma comment(lib, "CIGI_IG_InterfaceD_x86.lib")
#else
				#pragma comment(lib, "CIGI_IG_Interface_x86.lib")
#endif
#else
			#error "no machine type"
#endif
#endif

namespace CIGI_IG_Interface_NS
{
	class ICIGI_IG_Interface
	{
	public:
		virtual void SendAndReceiveMessage() =0;
		virtual ~ICIGI_IG_Interface() = default;
	};

	ICIGI_IG_Interface *CreateCIGI_IG_Interface(const Network_NS::InAddress_sptr& igAddress,
												const Network_NS::OutAddress_sptr& hostAddress,
												IEntityCtrlProcessor* entityCtrlProcessor,
												IHotHatProcessor* hotHatProcessor,
												IWeatherCondProcessor* weatherCondProcessor,
												ICompCtrlProcessor* compCtrlProcessor);
}
