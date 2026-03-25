#pragma once

namespace CIGI_IG_Interface_NS
{
    class IPause
    {
    public:
        virtual ~IPause() = default;
        virtual void TogglePause() = 0;
        virtual bool IG_IsPaused() = 0;
    };
}
