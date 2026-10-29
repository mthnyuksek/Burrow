#pragma once

#include <cstddef>
#include <string>

// Files compiled into the program (the Web folder plus the WebView2 loader DLL), see CMakeLists.txt.
// `path` is relative to the Web folder, e.g. "index.html". `mime` is guessed from the extension.
bool FindEmbeddedFile(const std::string& path, const unsigned char*& data, size_t& size, std::string& mime);
