#include "Ui/remote_server.h"

#include "Ui/embedded_web.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <thread>

namespace {
constexpr size_t kMaxHeaderBytes = 16 * 1024;
constexpr size_t kMaxBodyBytes = 2 * 1024 * 1024;
constexpr int kMaxConnections = 48;
constexpr int kPollSeconds = 25;

std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        --b;
    return s.substr(a, b - a);
}

bool SameSecret(const std::string& a, const std::string& b) {  // does not stop at the first difference
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

bool SendAll(SOCKET s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const int n = send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0)
            return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

void Respond(SOCKET s, int status, const char* reason, const std::string& mime, const std::string& body) {
    std::string head = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\nContent-Type: " + mime +
                       "\r\nContent-Length: " + std::to_string(body.size()) +
                       "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                       "Referrer-Policy: no-referrer\r\nConnection: close\r\n\r\n";
    SendAll(s, head + body);
}

std::string JsonError(const std::string& message) {
    std::string escaped;
    for (char c : message) {
        if (c == '"' || c == '\\')
            escaped += '\\';
        if (static_cast<unsigned char>(c) >= 0x20)
            escaped += c;
    }
    return "{\"ok\":false,\"error\":\"" + escaped + "\"}";
}
}  // namespace

struct RemoteServer::Shared {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<RemoteInvite> invites;
    std::map<std::string, long> last_seen;
    unsigned long version = 1;
    std::atomic<bool> stop{false};
    std::atomic<int> active{0};
    SOCKET listener = INVALID_SOCKET;
    ApiHandler on_api;

    bool Authenticate(const std::string& header, RemoteInvite& who) {
        static const std::string prefix = "Bearer ";
        if (header.compare(0, prefix.size(), prefix) != 0)
            return false;
        const std::string token = Trim(header.substr(prefix.size()));
        std::lock_guard<std::mutex> lock(mutex);
        for (const RemoteInvite& i : invites)
            if (SameSecret(i.token, token)) {
                who = i;
                last_seen[i.id] = static_cast<long>(std::time(nullptr));
                return true;
            }
        return false;
    }

    // One connection from request to answer.
    void Serve(SOCKET s) {
        DWORD timeout = 10000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);

        std::string data;
        size_t header_end = std::string::npos;
        char buffer[4096];
        while (header_end == std::string::npos && data.size() < kMaxHeaderBytes) {
            const int n = recv(s, buffer, sizeof buffer, 0);
            if (n <= 0)
                return;
            data.append(buffer, static_cast<size_t>(n));
            header_end = data.find("\r\n\r\n");
        }
        if (header_end == std::string::npos)
            return Respond(s, 431, "Header Too Large", "text/plain", "");

        const std::string head = data.substr(0, header_end);
        std::string body = data.substr(header_end + 4);
        const size_t line_end = head.find("\r\n");
        const std::string request_line = head.substr(0, line_end);
        const size_t sp1 = request_line.find(' ');
        const size_t sp2 = request_line.find(' ', sp1 + 1);
        if (sp1 == std::string::npos || sp2 == std::string::npos)
            return Respond(s, 400, "Bad Request", "text/plain", "");
        const std::string method = request_line.substr(0, sp1);
        std::string target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);

        std::string authorization;
        size_t content_length = 0;
        size_t pos = line_end == std::string::npos ? head.size() : line_end + 2;
        while (pos < head.size()) {
            size_t eol = head.find("\r\n", pos);
            if (eol == std::string::npos)
                eol = head.size();
            const std::string line = head.substr(pos, eol - pos);
            const size_t colon = line.find(':');
            if (colon != std::string::npos) {
                const std::string key = Lower(line.substr(0, colon));
                const std::string value = Trim(line.substr(colon + 1));
                if (key == "authorization")
                    authorization = value;
                else if (key == "content-length")
                    content_length = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10));
            }
            pos = eol + 2;
        }
        if (content_length > kMaxBodyBytes)
            return Respond(s, 413, "Payload Too Large", "text/plain", "");
        while (body.size() < content_length) {
            const int n = recv(s, buffer, sizeof buffer, 0);
            if (n <= 0)
                return;
            body.append(buffer, static_cast<size_t>(n));
        }
        body.resize(content_length);

        std::string query;
        const size_t q = target.find('?');
        if (q != std::string::npos) {
            query = target.substr(q + 1);
            target.erase(q);
        }

        if (target == "/api" || target == "/api/poll") {
            RemoteInvite who;
            if (!Authenticate(authorization, who))
                return Respond(s, 401, "Unauthorized", "application/json; charset=utf-8",
                               JsonError("Davet geçersiz ya da erişim kapatıldı."));
            if (target == "/api") {
                if (method != "POST")
                    return Respond(s, 405, "Method Not Allowed", "text/plain", "");
                return Respond(s, 200, "OK", "application/json; charset=utf-8", on_api(who, body, stop));
            }
            unsigned long seen = 0;
            if (query.rfind("v=", 0) == 0)
                seen = std::strtoul(query.c_str() + 2, nullptr, 10);
            unsigned long now_version = 0;
            {
                std::unique_lock<std::mutex> lock(mutex);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kPollSeconds);
                while (!stop && version == seen && std::chrono::steady_clock::now() < deadline)
                    changed.wait_for(lock, std::chrono::milliseconds(500));
                now_version = version;
            }
            return Respond(s, 200, "OK", "application/json; charset=utf-8",
                           "{\"v\":" + std::to_string(now_version) + "}");
        }

        // Everything else is the web interface itself.
        if (method != "GET" && method != "HEAD")
            return Respond(s, 405, "Method Not Allowed", "text/plain", "");
        std::string path = target.empty() ? "/" : target;
        path.erase(0, 1);
        if (path.empty())
            path = "index.html";
        if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos)
            return Respond(s, 400, "Bad Request", "text/plain", "");
        const unsigned char* file = nullptr;
        size_t size = 0;
        std::string mime;
        if (!FindEmbeddedFile("web/" + path, file, size, mime))
            return Respond(s, 404, "Not Found", "text/plain", "Bulunamadı");
        Respond(s, 200, "OK", mime, method == "HEAD" ? std::string() : std::string(reinterpret_cast<const char*>(file), size));
    }
};

RemoteServer::~RemoteServer() { Stop(); }

bool RemoteServer::Start(const std::vector<RemoteInvite>& invites, std::string& error) {
    if (shared_)
        return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        error = "Ağ katmanı başlatılamadı.";
        return false;
    }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // never reachable from the network directly
    addr.sin_port = 0;
    if (listener == INVALID_SOCKET || bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 ||
        listen(listener, 16) != 0) {
        error = "Yerel sunucu başlatılamadı.";
        if (listener != INVALID_SOCKET)
            closesocket(listener);
        WSACleanup();
        return false;
    }
    int len = sizeof addr;
    getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);

    auto shared = std::make_shared<Shared>();
    shared->invites = invites;
    shared->listener = listener;
    shared->on_api = on_api;
    shared_ = shared;
    ++shared->active;  // the accept loop counts too, so Stop waits for it

    std::thread([shared] {
        while (!shared->stop) {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(shared->listener, &set);
            timeval wait = {0, 200 * 1000};
            if (select(0, &set, nullptr, nullptr, &wait) <= 0)
                continue;
            SOCKET client = accept(shared->listener, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                continue;
            if (shared->active >= kMaxConnections) {
                closesocket(client);
                continue;
            }
            ++shared->active;
            std::thread([shared, client] {
                try {
                    shared->Serve(client);
                } catch (...) {
                }
                closesocket(client);
                --shared->active;
            }).detach();
        }
        --shared->active;
    }).detach();
    return true;
}

void RemoteServer::Stop() {
    if (!shared_)
        return;
    std::shared_ptr<Shared> shared = std::move(shared_);
    shared_.reset();
    shared->stop = true;
    shared->changed.notify_all();
    for (int i = 0; i < 100 && shared->active > 0; ++i)  // up to ~10 s for requests in flight
        Sleep(100);
    closesocket(shared->listener);
    WSACleanup();
    port_ = 0;
}

void RemoteServer::SetInvites(const std::vector<RemoteInvite>& invites) {
    if (!shared_)
        return;
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->invites = invites;
}

void RemoteServer::NotifyChanged() {
    if (!shared_)
        return;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        ++shared_->version;
    }
    shared_->changed.notify_all();
}

std::map<std::string, long> RemoteServer::LastSeen() const {
    if (!shared_)
        return {};
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->last_seen;
}

std::string RemoteServer::NewToken() {
    unsigned char bytes[24] = {};
    BCryptGenRandom(nullptr, bytes, sizeof bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char b : bytes) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}
