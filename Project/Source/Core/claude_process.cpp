#include "Core/claude_process.h"

#include <windows.h>

#include <algorithm>
#include <vector>

namespace {
std::wstring Widen(const std::string& s) {
    if (s.empty())
        return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

// A pipe whose `child_end` is inherited by the child and whose `our_end` is not.
bool MakePipe(HANDLE& our_end, HANDLE& child_end, bool child_reads) {
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &sa, 0))
        return false;
    our_end = child_reads ? write : read;
    child_end = child_reads ? read : write;
    SetHandleInformation(our_end, HANDLE_FLAG_INHERIT, 0);
    return true;
}

void CloseIf(HANDLE& h) {
    if (h && h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    h = nullptr;
}

bool Launch(std::wstring command, const std::wstring& cwd, STARTUPINFOW& si, PROCESS_INFORMATION& pi) {
    return CreateProcessW(nullptr, &command[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                          nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi) != 0;
}
}  // namespace

ClaudeProcess::ClaudeProcess() = default;

ClaudeProcess::~ClaudeProcess() {
    Stop();
    if (writer_.joinable())
        writer_.join();  // the pipe is broken once the process is gone, so the writer ends
    CloseHandles();
}

void ClaudeProcess::CloseHandles() {
    HANDLE h;
    h = static_cast<HANDLE>(process_); CloseIf(h); process_ = nullptr;
    h = static_cast<HANDLE>(job_); CloseIf(h); job_ = nullptr;
    h = static_cast<HANDLE>(out_read_); CloseIf(h); out_read_ = nullptr;
    h = static_cast<HANDLE>(err_read_); CloseIf(h); err_read_ = nullptr;
}

bool ClaudeProcess::Start(const std::string& command, const std::string& cwd, const std::string& stdin_text) {
    HANDLE in_ours = nullptr, in_child = nullptr, out_ours = nullptr, out_child = nullptr;
    HANDLE err_ours = nullptr, err_child = nullptr;
    if (!MakePipe(in_ours, in_child, true) || !MakePipe(out_ours, out_child, false) ||
        !MakePipe(err_ours, err_child, false)) {
        CloseIf(in_ours); CloseIf(in_child); CloseIf(out_ours); CloseIf(out_child);
        CloseIf(err_ours); CloseIf(err_child);
        return false;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_child;
    si.hStdOutput = out_child;
    si.hStdError = err_child;

    PROCESS_INFORMATION pi = {};
    const std::wstring wide_cwd = Widen(cwd);
    // Find the program first: a real .exe is started directly, an npm style .cmd shim needs the
    // command interpreter, and anything else cannot be started at all.
    const std::wstring program = Widen(command.substr(0, command.find(' ')));
    wchar_t found[MAX_PATH];
    bool started = false;
    if (SearchPathW(nullptr, program.c_str(), L".exe", MAX_PATH, found, nullptr) > 0)
        started = Launch(Widen(command), wide_cwd, si, pi);
    else if (SearchPathW(nullptr, program.c_str(), L".cmd", MAX_PATH, found, nullptr) > 0 ||
             SearchPathW(nullptr, program.c_str(), L".bat", MAX_PATH, found, nullptr) > 0)
        started = Launch(L"cmd.exe /d /s /c \"" + Widen(command) + L"\"", wide_cwd, si, pi);
    CloseIf(in_child);
    CloseIf(out_child);
    CloseIf(err_child);
    if (!started) {
        CloseIf(in_ours); CloseIf(out_ours); CloseIf(err_ours);
        return false;
    }

    // Everything the process starts belongs to one job, so Stop() can end the whole tree.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
        AssignProcessToJobObject(job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    process_ = pi.hProcess;
    job_ = job;
    pid_ = pi.dwProcessId;
    out_read_ = out_ours;
    err_read_ = err_ours;

    // The prompt can be larger than the pipe buffer, so a helper thread writes it and closes stdin.
    const HANDLE in = in_ours;
    writer_ = std::thread([in, stdin_text] {
        size_t done = 0;
        while (done < stdin_text.size()) {
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(stdin_text.size() - done, 1 << 16));
            if (!WriteFile(in, stdin_text.data() + done, chunk, &written, nullptr) || written == 0)
                break;
            done += written;
        }
        CloseHandle(in);  // end of input: Claude Code starts working when stdin is closed
    });
    return true;
}

void ClaudeProcess::Drain(void* pipe, std::string& buffer) {
    if (!pipe)
        return;
    char chunk[4096];
    while (true) {
        DWORD available = 0;
        if (!PeekNamedPipe(static_cast<HANDLE>(pipe), nullptr, 0, nullptr, &available, nullptr) || available == 0)
            return;
        DWORD got = 0;
        if (!ReadFile(static_cast<HANDLE>(pipe), chunk, sizeof chunk, &got, nullptr) || got == 0)
            return;
        buffer.append(chunk, got);
    }
}

void ClaudeProcess::EmitLines(bool flush) {
    size_t start = 0;
    while (true) {
        const size_t end = out_buffer_.find('\n', start);
        if (end == std::string::npos)
            break;
        if (end > start && on_line) {
            std::string line = out_buffer_.substr(start, end - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
                on_line(line);
        }
        start = end + 1;
    }
    out_buffer_.erase(0, start);
    if (flush && !out_buffer_.empty()) {
        if (on_line)
            on_line(out_buffer_);
        out_buffer_.clear();
    }
}

void ClaudeProcess::Poll() {
    if (!process_ || exited_)
        return;
    Drain(out_read_, out_buffer_);
    Drain(err_read_, stderr_);
    EmitLines(false);

    if (WaitForSingleObject(static_cast<HANDLE>(process_), 0) != WAIT_OBJECT_0)
        return;
    exited_ = true;
    DWORD code = 0;
    GetExitCodeProcess(static_cast<HANDLE>(process_), &code);
    Drain(out_read_, out_buffer_);  // whatever the process wrote last
    Drain(err_read_, stderr_);
    EmitLines(true);
    // The owner may destroy this object from inside on_exit, so it is the last thing that happens.
    std::function<void(int)> done = std::move(on_exit);
    on_exit = nullptr;
    if (done)
        done(static_cast<int>(code));
}

void ClaudeProcess::Stop() {
    if (job_)
        TerminateJobObject(static_cast<HANDLE>(job_), 1);
    else if (process_ && !exited_)
        TerminateProcess(static_cast<HANDLE>(process_), 1);
}
