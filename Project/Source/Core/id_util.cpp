#include "Core/id_util.h"

#include <cstdint>

namespace {
// Turkish letters have no plain ASCII form, so map them by hand.
char TurkishToAscii(uint32_t cp) {
    switch (cp) {
        case 0xE7: case 0xC7: return 'c';  // ç Ç
        case 0x11F: case 0x11E: return 'g';  // ğ Ğ
        case 0x131: case 0x130: return 'i';  // ı İ
        case 0xF6: case 0xD6: return 'o';  // ö Ö
        case 0x15F: case 0x15E: return 's';  // ş Ş
        case 0xFC: case 0xDC: return 'u';  // ü Ü
    }
    return 0;
}

// Next code point of a UTF-8 string; bad bytes come out as 0xFFFD.
uint32_t NextCodePoint(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80)
        return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (extra < 0)
        return 0xFFFD;
    uint32_t cp = c & (0x3F >> extra);
    for (; extra > 0 && i < s.size(); --extra)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    return cp;
}
}  // namespace

std::string MakeId(const std::string& name) {
    std::string id;
    bool last_sep = true;
    for (size_t i = 0; i < name.size();) {
        const uint32_t cp = NextCodePoint(name, i);
        char piece = TurkishToAscii(cp);
        if (!piece && cp >= 'a' && cp <= 'z')
            piece = static_cast<char>(cp);
        else if (!piece && cp >= 'A' && cp <= 'Z')
            piece = static_cast<char>(cp - 'A' + 'a');
        else if (!piece && cp >= '0' && cp <= '9')
            piece = static_cast<char>(cp);
        if (piece) {
            id += piece;
            last_sep = false;
        } else if (!last_sep) {
            id += '_';
            last_sep = true;
        }
    }
    while (!id.empty() && id.back() == '_')
        id.pop_back();
    return id.empty() ? std::string("item") : id;
}
