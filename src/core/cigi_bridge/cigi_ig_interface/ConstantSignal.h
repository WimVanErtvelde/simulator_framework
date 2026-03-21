#pragma once
#include <memory>

#include "BaseSignal.h"

namespace CIGI_IG_Interface_NS
{
	class ConstantSignal final
	{
		const double m_value;
		std::unique_ptr<IBaseSignal> m_baseSignal;

	public:
		explicit ConstantSignal(IBaseSignal* baseSignal, const double value)
			: m_value(value), m_baseSignal(baseSignal)
		{
		}

		void SetValue() const
		{
			m_baseSignal->SetValue(m_value);
		}
	};
}
