#include "Core/platform.h"

#include "Core/text.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace stdfs = std::filesystem;

namespace {
stdfs::path P(const std::string& utf8) { return stdfs::u8path(utf8); }

std::wstring Widen(const std::string& s) {
    if (s.empty())
        return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

std::string Narrow(const std::wstring& s) {
    if (s.empty())
        return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr);
    return out;
}

bool WriteWithMode(const std::string& path, const std::string& content, std::ios::openmode mode) {
    std::ofstream out(P(path), mode | std::ios::binary);
    if (!out)
        return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(out);
}
}  // namespace

std::wstring winstr::Widen(const std::string& utf8) { return ::Widen(utf8); }
std::string winstr::Narrow(const std::wstring& wide) { return ::Narrow(wide); }

int str::CmpNoCase(const std::string& a, const std::string& b) {
    std::wstring wa = Widen(a), wb = Widen(b);
    if (!wa.empty())
        CharLowerBuffW(&wa[0], static_cast<DWORD>(wa.size()));
    if (!wb.empty())
        CharLowerBuffW(&wb[0], static_cast<DWORD>(wb.size()));
    return wa.compare(wb);
}

// --- Files ----------------------------------------------------------------------------------

std::string fsx::Join(const std::string& dir, const std::string& file) {
    if (dir.empty())
        return file;
    return (P(dir) / P(file)).u8string();
}

bool fsx::ReadFile(const std::string& path, std::string& out) {
    std::ifstream in(P(path), std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool fsx::WriteFile(const std::string& path, const std::string& content) {
    return WriteWithMode(path, content, std::ios::out | std::ios::trunc);
}

bool fsx::AppendFile(const std::string& path, const std::string& content) {
    return WriteWithMode(path, content, std::ios::out | std::ios::app);
}

bool fsx::FileExists(const std::string& path) {
    std::error_code ec;
    return stdfs::is_regular_file(P(path), ec);
}

bool fsx::DirExists(const std::string& path) {
    std::error_code ec;
    return stdfs::is_directory(P(path), ec);
}

bool fsx::MakeDirs(const std::string& path) {
    std::error_code ec;
    stdfs::create_directories(P(path), ec);
    return DirExists(path);
}

bool fsx::Rename(const std::string& from, const std::string& to) {
    std::error_code ec;
    stdfs::rename(P(from), P(to), ec);
    return !ec;
}

void fsx::SetHidden(const std::string& path) {
    const std::wstring wide = winstr::Widen(path);
    const DWORD attrs = GetFileAttributesW(wide.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_HIDDEN))
        SetFileAttributesW(wide.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
}

bool fsx::RemoveFile(const std::string& path) {
    std::error_code ec;
    return stdfs::remove(P(path), ec) && !ec;
}

bool fsx::RemoveDirRecursive(const std::string& path) {
    std::error_code ec;
    stdfs::remove_all(P(path), ec);
    return !ec && !DirExists(path);
}

bool fsx::Copy(const std::string& from, const std::string& to) {
    std::error_code ec;
    stdfs::copy_file(P(from), P(to), stdfs::copy_options::overwrite_existing, ec);
    return !ec;
}

std::vector<std::string> fsx::ListFiles(const std::string& dir, const std::string& ext) {
    std::vector<std::string> names;
    std::error_code ec;
    for (stdfs::directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec))
            continue;
        const std::string name = it->path().filename().u8string();
        if (ext.empty() || (name.size() >= ext.size() &&
                            str::CmpNoCase(name.substr(name.size() - ext.size()), ext) == 0))
            names.push_back(name);
    }
    return names;
}

std::vector<std::string> fsx::ListDirs(const std::string& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (stdfs::directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (it->is_directory(type_ec))
            names.push_back(it->path().filename().u8string());
    }
    return names;
}

std::string fsx::Stem(const std::string& file_name) { return P(file_name).stem().u8string(); }

std::string fsx::LastDirName(const std::string& path) {
    stdfs::path p = P(path);
    if (p.has_relative_path() && p.filename().empty())  // "C:\x\Vault\" -> filename is empty
        p = p.parent_path();
    return p.filename().u8string();
}

bool fsx::IsRelative(const std::string& path) { return P(path).is_relative(); }

std::string fsx::MakeAbsolute(const std::string& path, const std::string& base) {
    const stdfs::path p = P(path);
    return p.is_relative() ? (P(base) / p).lexically_normal().u8string() : p.lexically_normal().u8string();
}

std::string fsx::TempDir() {
    std::error_code ec;
    return stdfs::temp_directory_path(ec).u8string();
}

std::string fsx::ParentOf(const std::string& path) { return P(path).parent_path().u8string(); }

std::string fsx::UserDataDir() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    std::string base = n > 0 && n < MAX_PATH ? Narrow(buffer) : TempDir();
    const std::string dir = Join(base, "burrow");
    MakeDirs(dir);
    return dir;
}

// --- Time -----------------------------------------------------------------------------------

time_t timeutil::Now() { return std::time(nullptr); }

std::string timeutil::Format(time_t t, const char* strftime_format) {
    std::tm local = {};
    localtime_s(&local, &t);
    char buffer[128];
    const size_t n = std::strftime(buffer, sizeof buffer, strftime_format, &local);
    return std::string(buffer, n);
}

std::string timeutil::IsoDate(time_t t) { return Format(t, "%Y-%m-%d"); }

time_t timeutil::Make(int year, int month, int day, int hour, int minute, int second) {
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

time_t timeutil::SetClock(time_t day, int hour, int minute) {
    std::tm tm = {};
    localtime_s(&tm, &day);
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

bool timeutil::SameDate(time_t a, time_t b) { return IsoDate(a) == IsoDate(b); }

int timeutil::Weekday(time_t t) {
    std::tm local = {};
    localtime_s(&local, &t);
    return (local.tm_wday + 6) % 7;  // tm_wday: 0 = Sunday
}

std::string timeutil::DateDaysBefore(const std::string& iso_date, int days) {
    int y = 0, m = 0, d = 0;
    if (std::sscanf(iso_date.c_str(), "%d-%d-%d", &y, &m, &d) != 3)
        return iso_date;
    std::tm tm = {};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d - days;
    tm.tm_hour = 12;  // noon: safe against daylight saving shifts
    tm.tm_isdst = -1;
    const time_t t = std::mktime(&tm);
    return IsoDate(t);
}
