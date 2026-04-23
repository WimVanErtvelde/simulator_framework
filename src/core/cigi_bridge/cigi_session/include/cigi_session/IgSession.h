// IG-side CIGI 3.3 session. Dispatches received Host→IG packets to
// registered processors. Plugin code registers one processor per packet
// class it handles.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace cigi_session {

class IIgCtrlProcessor;
class IEntityCtrlProcessor;
class IHatHotReqProcessor;
class IAtmosphereProcessor;
class IEnvRegionProcessor;
class IWeatherCtrlProcessor;
class ICompCtrlProcessor;

class IgSession {
public:
    IgSession();
    ~IgSession();
    IgSession(const IgSession &) = delete;
    IgSession & operator=(const IgSession &) = delete;

    void SetIgCtrlProcessor(IIgCtrlProcessor * p);
    void SetEntityCtrlProcessor(IEntityCtrlProcessor * p);
    void SetHatHotReqProcessor(IHatHotReqProcessor * p);
    void SetAtmosphereProcessor(IAtmosphereProcessor * p);
    void SetEnvRegionProcessor(IEnvRegionProcessor * p);
    void SetWeatherCtrlProcessor(IWeatherCtrlProcessor * p);
    void SetCompCtrlProcessor(ICompCtrlProcessor * p);

    std::size_t HandleDatagram(const std::uint8_t * data, std::size_t len);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cigi_session
