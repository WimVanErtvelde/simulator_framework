#pragma once
#include <XPLMDataAccess.h>

// Typed dataref accessor wrappers — cache the XPLMDataRef on construction
// and provide operator() for read access.

class XPLMDataRefAccessor_d
{
    XPLMDataRef m_ref;
public:
    explicit XPLMDataRefAccessor_d(const char * name) : m_ref(XPLMFindDataRef(name)) {}
    double operator()() const { return m_ref ? XPLMGetDatad(m_ref) : 0.0; }
};

class XPLMDataRefAccessor_f
{
    XPLMDataRef m_ref;
public:
    explicit XPLMDataRefAccessor_f(const char * name) : m_ref(XPLMFindDataRef(name)) {}
    float operator()() const { return m_ref ? XPLMGetDataf(m_ref) : 0.0f; }
};
