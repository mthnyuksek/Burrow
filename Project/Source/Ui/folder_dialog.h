#pragma once

#include <windows.h>

#include <string>

// The Windows folder picker. Returns false when the person cancels. Paths are UTF-8.
bool PickFolder(HWND owner, const std::string& title, std::string& path);
