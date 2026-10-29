#pragma once

#include <string>

// Turns a display name into a file-name-safe id: "Araştırmacı Ajan" -> "arastirmaci_ajan".
std::string MakeId(const std::string& name);
