#pragma once
// Minimal aircraft configuration used by XPlaneData.
// The full version reads an XML config file via tinyxml/Config.cpp.
// This stub provides the interface XPlaneData.cpp needs for a minimal plugin.
#include <list>
#include <memory>
#include <XPLMUtilities.h>

// Forward-declared to satisfy ConstantSignal include chain
namespace CIGI_IG_Interface_NS { class IBaseSignal; }

class XPlaneConfig
{
public:
    // Commands to execute after loading a new aircraft (e.g. engine start)
    const std::list<XPLMCommandRef> & CommandList()   const { return commands_; }

    // Constant dataref values to set after loading (empty in minimal config)
    const std::list<std::unique_ptr<CIGI_IG_Interface_NS::IBaseSignal>> & ConstantsList() const
    {
        return constants_;
    }

    // If true, atmosphere temperature and pressure are written to X-Plane datarefs
    bool UseTempPressure() const { return false; }

private:
    std::list<XPLMCommandRef> commands_;
    std::list<std::unique_ptr<CIGI_IG_Interface_NS::IBaseSignal>> constants_;
};
