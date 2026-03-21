#pragma once
// Globals.h — standalone stub replacing DSim's Globals.h.
// Provides LogMessage, trimSTL, splitSTLEx, WrapTrack and other
// utilities that A424Parser and other modules expect.

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cmath>

// ── Logging ────────────────────────────────────────────────────────────────
inline void LogMessage(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

// ── String utilities ───────────────────────────────────────────────────────

// Trim leading and trailing whitespace in-place.
inline void trimSTL(std::string& s)
{
    size_t end = s.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) { s.clear(); return; }
    s.erase(end + 1);
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start != std::string::npos) s.erase(0, start);
}

// Split a string by a single delimiter character; return the parts.
inline std::vector<std::string> splitSTLEx(const std::string& s, char delim)
{
    std::vector<std::string> result;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, delim))
        result.push_back(token);
    return result;
}

// ── Navigation maths ──────────────────────────────────────────────────────

// Wrap a track/bearing value into [0, 360).
inline double WrapTrack(double deg)
{
    deg = std::fmod(deg, 360.0);
    if (deg < 0.0) deg += 360.0;
    return deg;
}

// ── Compiler compatibility ─────────────────────────────────────────────────

// __fallthrough suppresses -Wimplicit-fallthrough.  Provide no-op for MSVC.
#ifndef __fallthrough
#  if defined(__GNUC__) && __GNUC__ >= 7
#    define __fallthrough __attribute__((fallthrough))
#  else
#    define __fallthrough /* fallthrough */
#  endif
#endif
