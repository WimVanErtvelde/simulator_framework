#pragma once

#include <map>
#include <list>
#include <string>

#include "ConstantSignal.h"
#include "tinyxml.h"

#include "ICompCtrlProcessor.h"
// ReSharper disable once CppUnusedIncludeDirective
#include "XmlHelper.h"
#include "InAddress.h"
#include "OutAddress.h"

namespace CIGI_IG_Interface_NS
{
	class Config : public ICompCtrlProcessor
	{
	protected:
		typedef std::map<unsigned short int, std::unique_ptr<std::string>> EntityIdMap;
		typedef std::map<unsigned long long, std::unique_ptr<IBaseSignal>> ShortCompMap;
		typedef std::list<std::unique_ptr<ConstantSignal>> ConstantsList;

		std::string m_loadErrorMsg;
		std::string m_documentPath;

		Network_NS::OutAddress_sptr m_hostAddress;
		Network_NS::InAddress_sptr m_igAddress;

		Network_NS::OutAddress_sptr m_repositionAddress;
		bool m_reposition;
		double m_repositionAltOffsetFt;

		EntityIdMap m_entityTypeMap;
		ShortCompMap m_shortCompMap;
		ConstantsList m_constantsList;

	public:
		explicit Config(const char* documentPath);
		~Config() override = default;

		virtual void Load();

		 friend std::ostream &operator<<(std::ostream& stream, const Config& config)
		 {
		 	if(config.m_loadErrorMsg.empty())
		 	{
		 		stream << "IG :" << *config.IgAddress() << "," << *config.HostAddress();
		 	}
		 	else
		 	{
		 		stream << "** Error: " << config.m_loadErrorMsg;
		 	}
		
		 	return stream;
		}

		const Network_NS::OutAddress_sptr &HostAddress() const { return m_hostAddress; }
		const Network_NS::InAddress_sptr &IgAddress() const { return m_igAddress; }
		const Network_NS::OutAddress_sptr &RepositionAddress() const { return m_repositionAddress; }

		bool Reposition() const { return m_reposition; }
		double RepositionAltOffsetFt() const { return m_repositionAltOffsetFt; }

		void ProcessCompCtrlItem(const CompCtrlItem& item) const override;
		const char *GetAircraftToLoad(EntityIdMap::key_type key) const;
		void SetConstants() const;

	private:
		void CreateEntityTypeMap(const std::unique_ptr<TiXmlHandle>& hConfig);
		void CreateShortCompMap(const std::unique_ptr<TiXmlHandle>& hConfig);
		void CreateConstantsList(const std::unique_ptr<TiXmlHandle>& hConfig);


	protected:
		virtual void CreateAdditional(const std::unique_ptr<TiXmlHandle>& hConfig)
		{
		}

		virtual IBaseSignal *CreateBaseSignal(TiXmlElement* child) = 0;
	};
}
