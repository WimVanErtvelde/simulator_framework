#pragma once
namespace CIGI_IG_Interface_NS
{
	class IBaseSignal
	{
	public:
		virtual ~IBaseSignal() = default;
		virtual void SetValue(double value) = 0;
	};
}
