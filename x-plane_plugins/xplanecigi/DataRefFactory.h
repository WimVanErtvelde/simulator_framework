#pragma once
#include <XPLMDataAccess.h>

// Thin factory that wraps XPLMFindDataRef — keeps XPlaneData.cpp clean of
// direct SDK calls for the initial find step.
struct DataRefFactory
{
    static XPLMDataRef GetDataRef(const char * name)
    {
        return XPLMFindDataRef(name);
    }
};
