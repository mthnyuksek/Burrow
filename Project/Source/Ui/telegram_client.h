#pragma once

#include "Core/telegram_config.h"

#include <string>
#include <vector>

// Telegram Bot API over WinHTTP (HTTPS, no library). Every call blocks until it is answered or
// times out, so the page-facing ones run from a command and the agent ones from a worker thread.
namespace telegram {

// Sends text to the configured chat. Returns an empty string on success, otherwise a short
// reason (Telegram's own description when it gave one). The token never appears in it.
std::string Send(const TelegramConfig& config, const std::string& text);

// Finds the chat that last wrote to the bot (the person has to message the bot first).
// On failure returns an empty id and fills `error`.
std::string DetectChatId(const std::string& token, std::string& error);

// One text message somebody wrote to the bot.
struct Incoming {
    std::string chat_id;
    std::string text;
};

// Asks for new messages, waiting up to wait_seconds for one (long poll). `offset` is the first
// update id wanted (0 = the oldest still kept); with -1 it only reports where the newest one is
// and returns nothing, which is how the backlog is skipped at start. next_offset is where the
// next call continues. Returns false with `error` set if the request failed.
bool GetUpdates(const std::string& token, long long offset, int wait_seconds, std::vector<Incoming>& messages,
                long long& next_offset, std::string& error);

}  // namespace telegram
