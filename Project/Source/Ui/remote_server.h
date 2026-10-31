#pragma once

#include "Core/remote_config.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// A tiny HTTP server for friends who join this Burrow from another computer (see remote_config.h).
// It listens on 127.0.0.1 only; the internet reaches it through the tunnel (tunnel.h), never directly.
//   GET  /...           the web interface (embedded files, public: they contain no secrets)
//   POST /api           {"cmd":..,"args":..} with "Authorization: Bearer <invite token>"
//   GET  /api/poll?v=N  answers {"v":M} when the program state changed since version N (long poll, 25 s)
// Every worker thread only parses HTTP; commands run on_api, which the owner must route to its UI thread.
class RemoteServer {
public:
    // Called on a worker thread with the invite that authenticated the call; returns the JSON answer.
    // `stop` turns true when the server shuts down: the handler must give up waiting then.
    using ApiHandler =
        std::function<std::string(const RemoteInvite& who, const std::string& body, const std::atomic<bool>& stop)>;
    ApiHandler on_api;

    RemoteServer() = default;
    ~RemoteServer();
    RemoteServer(const RemoteServer&) = delete;
    RemoteServer& operator=(const RemoteServer&) = delete;

    bool Start(const std::vector<RemoteInvite>& invites, std::string& error);
    void Stop();  // closes everything and waits for the worker threads
    bool Running() const { return shared_ != nullptr; }
    int Port() const { return port_; }

    void SetInvites(const std::vector<RemoteInvite>& invites);  // takes effect at once (revoked = locked out)
    void NotifyChanged();                                        // wakes the long polls
    std::map<std::string, long> LastSeen() const;                // invite id -> unix time of its latest call

    static std::string NewToken();  // 192 random bits as hex

private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
    int port_ = 0;
};
