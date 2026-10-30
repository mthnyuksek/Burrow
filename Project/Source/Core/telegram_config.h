#pragma once

#include <string>

// Telegram bot settings of this user (not of a vault: the token is a secret and a vault is a
// plain folder that may be copied or shared). Kept in %APPDATA%\burrow\telegram.ini as
// "Key=value" lines: Token=, ChatId=, NotifyFailures=0|1.
struct TelegramConfig {
    std::string token;
    std::string chat_id;
    bool notify_failures = false;  // tell the chat when a scheduled run fails

    bool Ready() const { return !token.empty() && !chat_id.empty(); }

    static TelegramConfig Load(const std::string& file);
    bool Save(const std::string& file) const;
};
