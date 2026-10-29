#pragma once

#include "Core/json.h"

// Short helpers for building and reading the JSON that goes over the web bridge.
inline Json JStr(const std::string& s) { return Json::MakeString(s); }
inline Json JNum(double n) { return Json::MakeNumber(n); }
inline Json JBool(bool b) { return Json::MakeBool(b); }

inline std::string WStr(const Json& j) { return j.AsString(); }
inline int WInt(const Json& j, int fallback = 0) { return static_cast<int>(j.AsNumber(fallback)); }
