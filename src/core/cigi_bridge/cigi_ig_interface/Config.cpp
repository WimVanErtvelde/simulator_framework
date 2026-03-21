#include "Config.h"

#include "ConstantSignal.h"
using namespace CIGI_IG_Interface_NS;
using namespace Network_NS;

Config::Config(const char* documentPath)
	: m_documentPath(documentPath),
	m_reposition(false),
	m_repositionAltOffsetFt(0)
{
}

void Config::Load()
{
	TiXmlDocument doc(m_documentPath.c_str());

	if(!doc.LoadFile())
	{
		m_loadErrorMsg = std::string("missing file ") + m_documentPath;
		return;
	}

	TiXmlNode* node = doc.FirstChild("IG_Initialization");

	if(node == nullptr)
	{
		m_loadErrorMsg = "No IG_Initialization node";
		return;
	}

	const std::unique_ptr<TiXmlHandle> hConfig = std::make_unique<TiXmlHandle>(node);

	const TiXmlElement* hostAddr = hConfig->FirstChildElement("HostAddress").ToElement();
	m_hostAddress = make_OutAddress_sptr(hostAddr);

	const TiXmlElement* igAddr = hConfig->FirstChildElement("IG_Address").ToElement();
	m_igAddress = make_InAddress_sptr(igAddr);

	const TiXmlElement* reposition = hConfig->FirstChildElement("Reposition").ToElement();

	if(reposition != nullptr)
	{
		const int on = strtol(GetAttribute(reposition, "On"), nullptr, 10);
		m_reposition = on == 1;

		int port = strtol(GetAttribute(reposition, "Port"), nullptr, 10);
		m_repositionAddress = make_OutAddress_sptr(m_hostAddress->IP, port);
		m_repositionAltOffsetFt = strtol(GetAttribute(reposition, "AltOffsetFt"), nullptr, 10);
	}

	CreateEntityTypeMap(hConfig);
	CreateShortCompMap(hConfig);
	CreateConstantsList(hConfig);
	CreateAdditional(hConfig);
}

void Config::CreateEntityTypeMap(const std::unique_ptr<TiXmlHandle>& hConfig)
{
	for(TiXmlElement* child = hConfig->FirstChildElement("EntityTypes").FirstChildElement("EntityType").ToElement(); child != nullptr; child = child->NextSiblingElement("EntityType"))
	{
		unsigned short id = static_cast<unsigned short>(strtol(GetAttribute(child, "Id"), nullptr, 10));
		m_entityTypeMap[id] = std::make_unique<std::string>(GetAttribute(child, "Load"));
	}
}

void Config::CreateShortCompMap(const std::unique_ptr<TiXmlHandle>& hConfig)
{
	for(TiXmlElement* child = hConfig->FirstChildElement("Comps").FirstChildElement("ShortComp").ToElement(); child != nullptr; child = child->NextSiblingElement("ShortComp"))
	{
		CompCtrlKey key;
		key.compClass = static_cast<unsigned char>(strtol(GetAttribute(child, "CompClass"), nullptr, 10));
		key.compId = static_cast<unsigned short>(strtol(GetAttribute(child, "CompId"), nullptr, 10));
		key.instanceID = static_cast<unsigned short>(strtol(GetAttribute(child, "InstanceID"), nullptr, 10));
		key.pos = 0;

		IBaseSignal* data = CreateBaseSignal(child);

		if(data != nullptr)
		{
			m_shortCompMap[key.key] = std::unique_ptr<IBaseSignal>(data);
		}
	}
}

void Config::CreateConstantsList(const std::unique_ptr<TiXmlHandle>& hConfig)
{
	for(TiXmlElement* child = hConfig->FirstChildElement("Constants").FirstChildElement("Constant").ToElement(); child != nullptr; child = child->NextSiblingElement("Constant"))
	{
		double value;
		child->Attribute("Value", &value);
		IBaseSignal* baseSignal = CreateBaseSignal(child);

		if(baseSignal != nullptr)
		{
			m_constantsList.push_back(std::make_unique<ConstantSignal>(baseSignal, value));
		}
	}
}



const char *Config::GetAircraftToLoad(const EntityIdMap::key_type key) const
{
	const EntityIdMap::const_iterator i = m_entityTypeMap.find(key);

	if(i != m_entityTypeMap.end())
	{
		return i->second->c_str();
	}

	return nullptr;
}

void Config::ProcessCompCtrlItem(const CompCtrlItem& item) const
{
	const ShortCompMap::const_iterator i = m_shortCompMap.find(item.key.key);

	if(i != m_shortCompMap.end())
	{
		i->second->SetValue(item.value);
	}
}

void Config::SetConstants() const
{
	for(const auto& data : m_constantsList)
	{
		data->SetValue();
	}
}
