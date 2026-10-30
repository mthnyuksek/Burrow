#include "Core/telegram_config.h"

#include "Core/platform.h"
#include "Core/text.h"

TelegramConfig TelegramConfig::Load(const std::string& file) {
    TelegramConfig c;
    std::string content;
    if (!fsx::ReadFile(file, content))
        return c;
    for (const std::string& raw : str::Split(content, '\n')) {
        const std::string line = str::Trim(raw);
        if (str::StartsWith(line, "Token="))
            c.token = str::Trim(line.substr(6));
        else if (str::StartsWith(line, "ChatId="))
            c.chat_id = str::Trim(line.substr(7));
        else if (str::StartsWith(line, "NotifyFailures="))
            c.notify_failures = str::Trim(line.substr(15)) == "1";
    }
    return c;
}

bool TelegramConfig::Save(const std::string& file) const {
    return fsx::WriteFile(file, "[Telegram]\nToken=" + token + "\nChatId=" + chat_id +
                                    "\nNotifyFailures=" + (notify_failures ? "1" : "0") + "\n");
}
