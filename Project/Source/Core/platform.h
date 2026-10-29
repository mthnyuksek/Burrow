#pragma once

#include <ctime>
#include <string>
#include <vector>

// Files, folders and local time on Windows. Paths and strings are UTF-8.

namespace fsx {
std::string Join(const std::string& dir, const std::string& file);
bool ReadFile(const std::string& path, std::string& out);
bool WriteFile(const std::string& path, const std::string& content);
bool AppendFile(const std::string& path, const std::string& content);
bool FileExists(const std::string& path);
bool DirExists(const std::string& path);
bool MakeDirs(const std::string& path);  // the whole chain; true if it exists afterwards
bool RemoveFile(const std::string& path);
bool RemoveDirRecursive(const std::string& path);
bool Copy(const std::string& from, const std::string& to);
// File names (not paths) in dir whose extension is ext (".md", case-insensitive); empty if dir is missing.
std::vector<std::string> ListFiles(const std::string& dir, const std::string& ext);
std::vector<std::string> ListDirs(const std::string& dir);  // sub folder names
std::string Stem(const std::string& file_name);              // "a.md" -> "a"
std::string LastDirName(const std::string& path);            // "C:\x\Vault" -> "Vault"
bool IsRelative(const std::string& path);
std::string MakeAbsolute(const std::string& path, const std::string& base);
std::string TempDir();
std::string UserDataDir();  // %APPDATA%\burrow, created on demand
std::string ParentOf(const std::string& path);
}  // namespace fsx

namespace winstr {
std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& wide);
}  // namespace winstr

namespace timeutil {
time_t Now();
std::string Format(time_t t, const char* strftime_format);  // local time
std::string IsoDate(time_t t);                              // "2026-10-29"
time_t Make(int year, int month, int day, int hour = 0, int minute = 0, int second = 0);  // local time
time_t SetClock(time_t day, int hour, int minute);          // same local date, other time of day
bool SameDate(time_t a, time_t b);
std::string DateDaysBefore(const std::string& iso_date, int days);  // unparsable input is returned as it is
}  // namespace timeutil
