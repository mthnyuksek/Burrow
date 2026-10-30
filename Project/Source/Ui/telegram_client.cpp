#include "Ui/telegram_client.h"

#include "Core/json.h"
#include "Core/platform.h"
#include "Core/text.h"

#include <cctype>
#include <windows.h>
#include <winhttp.h>

namespace telegram {
namespace {
constexpr int kResolveMs = 5000;
constexpr int kConnectMs = 5000;
constexpr int kSendMs = 10000;
constexpr int kReceiveMs = 15000;
constexpr size_t kMaxMessageChars = 4000;  // Telegram allows 4096

std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// One HTTPS request to api.telegram.org. Returns false with `error` set when it could not be made;
// otherwise `body` holds the answer (Telegram answers errors with JSON too).
bool Request(const std::string& token, const std::string& method, const std::string& form, std::string& body,
             std::string& error) {
    body.clear();
    HINTERNET session = WinHttpOpen(L"burrow", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = "İnternet bağlantısı başlatılamadı.";
        return false;
    }
    WinHttpSetTimeouts(session, kResolveMs, kConnectMs, kSendMs, kReceiveMs);
    HINTERNET connection = WinHttpConnect(session, L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = nullptr;
    bool ok = false;
    if (connection) {
        const std::wstring path = winstr::Widen("/bot" + token + "/" + method);
        request = WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    }
    if (request) {
        const wchar_t* headers = L"Content-Type: application/x-www-form-urlencoded";
        ok = WinHttpSendRequest(request, headers, static_cast<DWORD>(-1), const_cast<char*>(form.data()),
                                static_cast<DWORD>(form.size()), static_cast<DWORD>(form.size()), 0) &&
             WinHttpReceiveResponse(request, nullptr);
    }
    if (ok) {
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, &chunk[0], available, &read) || read == 0)
                break;
            body.append(chunk, 0, read);
        }
    } else {
        error = "Telegram'a ulaşılamadı (internet bağlantını kontrol et).";
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

// "" when Telegram says ok, otherwise its description (or a generic text).
std::string ErrorOf(const Json& answer) {
    if (answer["ok"].AsBool())
        return std::string();
    const std::string description = answer["description"].AsString();
    return description.empty() ? "Telegram isteği kabul etmedi." : "Telegram: " + description;
}
}  // namespace

std::string Send(const TelegramConfig& config, const std::string& text) {
    if (!config.Ready())
        return "Telegram ayarlanmamış (bot jetonu ve sohbet kimliği gerekli).";
    const std::string shown = str::Utf8Length(text) > kMaxMessageChars ? str::Utf8Left(text, kMaxMessageChars) + "…" : text;
    std::string body, error;
    if (!Request(config.token, "sendMessage", "chat_id=" + UrlEncode(config.chat_id) + "&text=" + UrlEncode(shown),
                 body, error))
        return error;
    Json answer;
    if (!Json::Parse(body, answer))
        return "Telegram'dan anlaşılmaz cevap geldi.";
    return ErrorOf(answer);
}

std::string DetectChatId(const std::string& token, std::string& error) {
    std::string body;
    if (token.empty()) {
        error = "Önce bot jetonunu yaz.";
        return std::string();
    }
    if (!Request(token, "getUpdates", "limit=20", body, error))
        return std::string();
    Json answer;
    if (!Json::Parse(body, answer)) {
        error = "Telegram'dan anlaşılmaz cevap geldi.";
        return std::string();
    }
    error = ErrorOf(answer);
    if (!error.empty())
        return std::string();
    const Json& updates = answer["result"];
    for (size_t i = updates.Size(); i > 0; --i) {  // newest update first
        const Json& chat = updates.At(i - 1)["message"]["chat"];
        if (chat["id"].type() == Json::Type::Number)
            return str::Format("%.0f", chat["id"].AsNumber());
    }
    error = "Bota henüz mesaj gelmemiş: Telegram'da botuna bir mesaj yaz, sonra tekrar dene.";
    return std::string();
}

}  // namespace telegram
