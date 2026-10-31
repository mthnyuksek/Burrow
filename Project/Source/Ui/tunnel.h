#pragma once

#include <memory>
#include <string>

// Makes the local remote server reachable from the internet with a free Cloudflare "quick tunnel":
// `cloudflared tunnel --url http://127.0.0.1:<port>` prints a random https://xxxx.trycloudflare.com
// address. No account, no router settings. The program (cloudflared.exe, ~40 MB) is not shipped with
// Burrow: Install() downloads it from Cloudflare's GitHub release into %LOCALAPPDATA%\burrow\runtime
// when the owner asks for it. The child process dies with Burrow (job object).
class Tunnel {
public:
    enum class State { Off, Installing, Starting, On, Error };
    struct Status {
        State state = State::Off;
        std::string url;      // https address while On
        std::string message;  // what went wrong, or progress text
    };

    Tunnel();
    ~Tunnel();
    Tunnel(const Tunnel&) = delete;
    Tunnel& operator=(const Tunnel&) = delete;

    static bool Installed();  // cloudflared.exe found (runtime folder or PATH)
    void Install();           // background download; Status shows Installing, then Off or Error
    void Start(int local_port);
    void Stop();
    Status Get() const;

private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
};
