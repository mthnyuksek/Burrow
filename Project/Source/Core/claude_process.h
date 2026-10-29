#pragma once

#include <functional>
#include <string>
#include <thread>

// One running `claude -p` child process. The prompt goes in through stdin, the stream-json output
// comes back line by line. Poll() must be called regularly (the RunManager does that from its
// tick); it reports the exit through on_exit, after which the object may be destroyed.
class ClaudeProcess {
public:
    ClaudeProcess();
    ~ClaudeProcess();
    ClaudeProcess(const ClaudeProcess&) = delete;
    ClaudeProcess& operator=(const ClaudeProcess&) = delete;

    std::function<void(const std::string& line)> on_line;
    std::function<void(int exit_code)> on_exit;

    // Starts command in cwd and feeds stdin_text to it. Returns false if it could not start.
    bool Start(const std::string& command, const std::string& cwd, const std::string& stdin_text);
    void Poll();
    void Stop();  // kills the process and its children

    const std::string& StdErr() const { return stderr_; }
    unsigned long Pid() const { return pid_; }

private:
    void Drain(void* pipe, std::string& buffer);
    void EmitLines(bool flush);
    void CloseHandles();

    void* process_ = nullptr;  // HANDLE values, kept as void* to keep windows.h out of this header
    void* job_ = nullptr;
    void* out_read_ = nullptr;
    void* err_read_ = nullptr;
    unsigned long pid_ = 0;
    bool exited_ = false;
    std::thread writer_;
    std::string out_buffer_;
    std::string stderr_;
};
