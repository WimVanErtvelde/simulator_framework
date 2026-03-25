#pragma once

namespace CIGI_IG_Interface_NS
{
    class IRepositionAccessor
    {
    public:
        virtual ~IRepositionAccessor() = default;
        virtual double GetLat()   const = 0;
        virtual double GetLon()   const = 0;
        virtual double GetAlt()   const = 0;
        virtual float  GetPhi()   const = 0;
        virtual float  GetTheta() const = 0;
        virtual float  GetPsi()   const = 0;
        virtual float  GetTas()   const = 0;
    };
}
