#pragma once

namespace CIGI_IG_Interface_NS
{
	class IHotHatProcessor
	{
	public:
		virtual ~IHotHatProcessor() = default;
		virtual void GetHot(double lat, double lon, double alt, double& hot, bool& valid) const = 0;
		virtual void GetHat(double lat, double lon, double alt, double& hat, bool& valid) const = 0;
	};
}
