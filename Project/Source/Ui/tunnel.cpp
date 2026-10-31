#include "Ui/tunnel.h"

#include "Core/platform.h"

#include <windows.h>
#include <winhttp.h>

#include <mutex>
#include <thread>

namespace {
constexpr char kDownloadHost[] = "github.com";
constexpr char kDownloadPath[] = "/cloudflare/cloudflared/releases/latest/download/cloudflared-windows-amd64.exe";
constexpr size_t kMinExeBytes = 10 * 1024 * 1024;  // the real file is ~40 MB: anything smaller is an error page

std::string RuntimeDir() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return std::string();
    return fsx::Join(fsx::Join(winstr::Narrow(buffer), "burrow"), "runtime");
}

std::string ExePath() {
    const std::string own = fsx::Join(RuntimeDir(), "cloudflared.exe");
    if (!RuntimeDir().empty() && fsx::FileExists(own))
        return own;
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"cloudflared.exe", nullptr, MAX_PATH, found, nullptr) > 0)
        return winstr::Narrow(found);
    return std::string();
}

// "...https://quiet-river-1234.trycloudflare.com..." -> the address; "" if the text has none.
std::string FindTunnelUrl(const std::string& text) {
    const std::string suffix = ".trycloudflare.com";
    size_t from = 0;
    while (true) {
        const size_t end = text.find(suffix, from);
        if (end == std::string::npos)
            return std::string();
        from = end + suffix.size();
        const size_t start = text.rfind("https://", end);
        if (start == std::string::npos)
            continue;
        const std::string host = text.substr(start + 8, end - start - 8);
        if (host.empty() || host == "api" || host.find_first_of(" \t\r\n/|") != std::string::npos)
            continue;
        return text.substr(start, end + suffix.size() - start);
    }
}

bool Download(std::string& data, std::string& error) {
    HINTERNET session = WinHttpOpen(L"burrow", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = "İnternet bağlantısı başlatılamadı.";
        return false;
    }
    WinHttpSetTimeouts(session, 8000, 8000, 30000, 60000);
    HINTERNET connection = WinHttpConnect(session, winstr::Widen(kDownloadHost).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", winstr::Widen(kDownloadPath).c_str(),
                                                        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        WINHTTP_FLAG_SECURE)
                                    : nullptr;
    bool ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);
    if (ok) {
        DWORD status = 0, size = sizeof status;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        ok = status == 200;
        if (!ok)
            error = "İndirme sunucusu hata verdi (" + std::to_string(status) + ").";
    } else {
        error = "cloudflared indirilemedi (internet bağlantını kontrol et).";
    }
    while (ok) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
            break;
        if (available == 0)
            break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, &chunk[0], available, &read) || read == 0)
            break;
        data.append(chunk, 0, read);
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (error.empty() && (data.size() < kMinExeBytes || data.compare(0, 2, "MZ") != 0))
        error = "İndirilen dosya cloudflared programı gibi görünmüyor.";
    return error.empty();
}
}  // namespace

struct Tunnel::Shared {
    mutable std::mutex mutex;
    Status status;
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    unsigned generation = 0;  // a reader thread of an older run must not touch the status

    void Set(State state, const std::string& url, const std::string& message) {
        status.state = state;
        status.url = url;
        status.message = message;
    }
    void KillLocked() {
        if (process) {
            TerminateProcess(process, 0);
            CloseHandle(process);
            process = nullptr;
        }
        if (job) {
            CloseHandle(job);  // KILL_ON_JOB_CLOSE ends the whole tree
            job = nullptr;
        }
    }
};

Tunnel::Tunnel() : shared_(std::make_shared<Shared>()) {}

Tunnel::~Tunnel() { Stop(); }

bool Tunnel::Installed() { return !ExePath().empty(); }

Tunnel::Status Tunnel::Get() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->status;
}

void Tunnel::Install() {
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        if (shared_->status.state == State::Installing)
            return;
        shared_->Set(State::Installing, "", "cloudflared indiriliyor (yaklaşık 40 MB)...");
    }
    std::thread([shared = shared_] {
        std::string data, error;
        bool ok = Download(data, error);
        if (ok) {
            const std::string dir = RuntimeDir();
            const std::string target = fsx::Join(dir, "cloudflared.exe");
            const std::string partial = target + ".part";
            ok = !dir.empty() && fsx::MakeDirs(dir) && fsx::WriteFile(partial, data);
            if (ok) {
                fsx::RemoveFile(target);
                ok = MoveFileExW(winstr::Widen(partial).c_str(), winstr::Widen(target).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
            }
            if (!ok)
                error = "cloudflared kaydedilemedi.";
        }
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->status.state == State::Installing)
            shared->Set(ok ? State::Off : State::Error, "", ok ? "cloudflared kuruldu." : error);
    }).detach();
}

void Tunnel::Start(int local_port) {
    Stop();
    const std::string exe = ExePath();
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (exe.empty()) {
        shared_->Set(State::Error, "", "cloudflared kurulu değil.");
        return;
    }
    SECURITY_ATTRIBUTES inherit = {sizeof inherit, nullptr, TRUE};
    HANDLE read_end = nullptr, write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &inherit, 0)) {
        shared_->Set(State::Error, "", "Tünel başlatılamadı.");
        return;
    }
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = INVALID_HANDLE_VALUE;
    std::wstring command = L"\"" + winstr::Widen(exe) + L"\" tunnel --no-autoupdate --url http://127.0.0.1:" +
                           std::to_wstring(local_port);
    PROCESS_INFORMATION pi = {};
    const BOOL started = CreateProcessW(nullptr, &command[0], nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
    CloseHandle(write_end);
    if (!started) {
        CloseHandle(read_end);
        shared_->Set(State::Error, "", "cloudflared başlatılamadı.");
        return;
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
        AssignProcessToJobObject(job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    shared_->process = pi.hProcess;
    shared_->job = job;
    const unsigned generation = ++shared_->generation;
    shared_->Set(State::Starting, "", "Tünel açılıyor...");

    std::thread([shared = shared_, read_end, generation] {
        std::string pending, tail;
        char buffer[2048];
        DWORD n = 0;
        while (ReadFile(read_end, buffer, sizeof buffer, &n, nullptr) && n > 0) {
            pending.append(buffer, n);
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (shared->generation != generation)
                break;
            if (shared->status.state == State::Starting) {
                const std::string url = FindTunnelUrl(pending);
                if (!url.empty())
                    shared->Set(State::On, url, "");
            }
            if (pending.size() > 8192)
                pending.erase(0, pending.size() - 4096);
            tail = pending.size() > 300 ? pending.substr(pending.size() - 300) : pending;
        }
        CloseHandle(read_end);
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->generation == generation && shared->status.state != State::Off) {
            shared->KillLocked();
            shared->Set(State::Error, "", "Tünel kapandı. " + tail);
        }
    }).detach();
}

void Tunnel::Stop() {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    ++shared_->generation;
    shared_->KillLocked();
    if (shared_->status.state != State::Installing)
        shared_->Set(State::Off, "", "");
}
