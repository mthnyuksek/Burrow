#include "Ui/embedded_web.h"

#include <windows.h>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace {
std::string MimeOf(const std::string& path) {
    const size_t dot = path.rfind('.');
    const std::string ext = dot == std::string::npos ? std::string() : path.substr(dot + 1);
    if (ext == "html") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js" || ext == "mjs") return "text/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "ico") return "image/x-icon";
    if (ext == "woff2") return "font/woff2";
    if (ext == "woff") return "font/woff";
    if (ext == "txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

// Same conversion as in CMakeLists.txt: "web/pages/chats.js" -> "WEB_PAGES_CHATS_JS".
std::string ResourceName(const std::string& path) {
    std::string name;
    for (const char c : path)
        name += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_';
    return name;
}
}  // namespace

bool FindEmbeddedFile(const std::string& path, const unsigned char*& data, size_t& size, std::string& mime) {
    // Development shortcut: with BURROW_WEB_DIR set to the Web folder the files are read from
    // disk, so a page edit needs no rebuild (reload with Ctrl+R in the window).
    static const char* const dev_dir = std::getenv("BURROW_WEB_DIR");
    if (dev_dir && path.rfind("web/", 0) == 0) {
        static std::string buffer;  // valid until the next call; callers copy it at once
        std::ifstream in(std::string(dev_dir) + "/" + path.substr(4), std::ios::binary);
        if (!in)
            return false;
        buffer.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        data = reinterpret_cast<const unsigned char*>(buffer.data());
        size = buffer.size();
        mime = MimeOf(path);
        return true;
    }
    HRSRC res = FindResourceA(nullptr, ResourceName(path).c_str(), reinterpret_cast<LPCSTR>(RT_RCDATA));
    if (!res)
        return false;
    HGLOBAL loaded = LoadResource(nullptr, res);
    if (!loaded)
        return false;
    data = static_cast<const unsigned char*>(LockResource(loaded));
    size = SizeofResource(nullptr, res);
    mime = MimeOf(path);
    return data != nullptr;
}
