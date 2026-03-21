#pragma once
#include "tinyxml.h"

inline const char *GetAttribute(const TiXmlElement* element, const char* name)
{
	const char* rtn = element->Attribute(name);

	if(rtn == nullptr)
	{
		const auto message = "Unknown attribute " + std::string(name) + " on " + element->ValueStr();
		throw std::runtime_error(message);
	}

	return rtn;
}
