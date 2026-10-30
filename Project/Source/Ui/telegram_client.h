#pragma once

#include "Core/telegram_config.h"

#include <string>

// Telegram Bot API over WinHTTP (HTTPS, no library). Every call blocks until it is answered or
// times out, so the page-facing ones run from a command and the agent ones from a worker thread.
namespace telegram {

// Sends text to the configured chat. Returns an empty string on success, otherwise a short
// reason (Telegram's own description when it gave one). The token never appears in it.
std::string Send(const TelegramConfig& config, const std::string& text);

// Finds the chat that last wrote to the bot (the person has to message the bot first).
// On failure returns an empty id and fills `error`.
std::string DetectChatId(const std::string& token, std::string& error);

}  // namespace telegram
