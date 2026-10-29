#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Small string helpers. All strings in the program are UTF-8 std::string.
namespace str {

inline bool IsSpace(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && IsSpace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && IsSpace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

// Lower-cases ASCII letters only (ids, language codes, "true"); use CmpNoCase for names.
inline std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

inline bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool Contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

inline std::string ReplaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty())
        return s;
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
        s.replace(at, from.size(), to);
    return s;
}

// "a,b,,c" -> {"a","b","","c"}; an empty string gives an empty list.
inline std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    if (s.empty())
        return parts;
    size_t start = 0;
    while (true) {
        const size_t end = s.find(sep, start);
        if (end == std::string::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

inline std::string BeforeFirst(const std::string& s, char c) {
    const size_t at = s.find(c);
    return at == std::string::npos ? s : s.substr(0, at);
}

inline std::string AfterFirst(const std::string& s, char c) {
    const size_t at = s.find(c);
    return at == std::string::npos ? std::string() : s.substr(at + 1);
}

// The whole string must be a number (leading blanks are fine), like wxString::ToLong.
inline bool ToLong(const std::string& s, long& out) {
    if (s.empty())
        return false;
    char* end = nullptr;
    const long n = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0')
        return false;
    out = n;
    return true;
}

inline bool ToDouble(const std::string& s, double& out) {
    if (s.empty())
        return false;
    char* end = nullptr;
    const double n = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0')
        return false;
    out = n;
    return true;
}

// printf into a std::string.
inline std::string Format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline std::string Format(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string out(n > 0 ? static_cast<size_t>(n) : 0, '\0');
    if (n > 0)
        std::vsnprintf(&out[0], out.size() + 1, fmt, args);
    va_end(args);
    return out;
}

// Number of Unicode characters (code points) in a UTF-8 string.
inline size_t Utf8Length(const std::string& s) {
    size_t n = 0;
    for (const char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            ++n;
    return n;
}

// The first `count` characters of a UTF-8 string.
inline std::string Utf8Left(const std::string& s, size_t count) {
    size_t seen = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (seen == count)
                return s.substr(0, i);
            ++seen;
        }
    }
    return s;
}

// Case-insensitive comparison that knows Turkish and other non-ASCII letters (defined in platform.cpp).
int CmpNoCase(const std::string& a, const std::string& b);

}  // namespace str
