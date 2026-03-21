#include "DME_Receiver.h"
#include <math.h>
#include <chrono>

// GetTickCount64 is a Windows API — include the header on Windows,
// provide a portable chrono-based replacement on Linux/macOS.
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
static uint64_t GetTickCount64()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
#endif

AS::DME_Receiver::DME_Receiver(AS::World* world, Model* model) : AS::AbstractReceiver(world, model)
{
}

void AS::DME_Receiver::updateRadio(int radio)
{
	if (freqOnLastUpdate != mModel->getFrequency(radio) || (GetTickCount64() > ticksOnLastUpdate + updateInterval))
	{
		updateRadioDME(radio);
		freqOnLastUpdate = mModel->getFrequency(radio);
		ticksOnLastUpdate = GetTickCount64();
	}
}

void AS::DME_Receiver::updateRadioDME(int radio)
{
	// get aircraft and radio parameters
	LatLon position(mModel->getLat(), mModel->getLon());
	int frequency = mModel->getFrequency(radio);

	// get all VOR's with given frequency within range
	std::vector<DME> candidates = mWorld->getDME(frequency, position);

	// results
	int found = 0;
	float distance = 0;

	// check all candidates for a suitable one
	for (unsigned int i = 0; i < candidates.size(); i++)
	{
		found = 1;

		DME dme = candidates[i];
		distance = LatLon::calculateSlantRangeNM(position, mModel->getAltitude(), dme.mLatLon, dme.mElevation);

		// terrain line-of-sight check (UHF — strict LOS required)
		// dme.mElevation is in metres (ft_to_m called in DME constructor)
		if(mLOS)
		{
			float navaid_elev_ft = (float)m_to_ft(dme.mElevation);
			if(!mLOS->hasLOS(position, mModel->getAltitude(), dme.mLatLon, navaid_elev_ft))
			{
				found = 0;
				distance = 0;
				continue;
			}
		}

		break;   // found a valid DME with LOS
	}

	// send values to Ice
	mModel->setDME_Found(radio, found);
	mModel->setDME_Distance(radio, distance);
}