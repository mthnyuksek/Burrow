#pragma once

#include "Core/claude_process.h"
#include "Core/usage_info.h"

#include <chrono>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

enum class JobSource { Chat, Manual, Scheduled, Agent, Probe };
std::string JobSourceLabel(JobSource source);

enum class RunStatus { Done, Failed, Skipped, Stopped };
std::string RunStatusLabel(RunStatus status);  // tamamlandı / hata / atlandı / durduruldu

// Who receives an answer and where it is written down. agent_id is "user" for the person
// chatting in conversation_id, otherwise the id of the agent that asked.
struct ReplyFrame {
    std::string agent_id;
    std::string conversation_id;
};

// Everything needed to run Claude Code once.
struct RunJob {
    long id = 0;  // set by Enqueue; names the job for CancelJob
    JobSource source = JobSource::Manual;
    std::string agent_id;
    std::string agent_name;
    std::string task_name;  // empty for chats and plain messages
    std::string project_id;  // empty = not working inside a project

    // Whom the answer of this job goes to. back() is the receiver; the frames below it receive
    // the answers that follow: user asks A, A asks B  ->  B's job has [user, A]. B's answer goes
    // to A, A's next job has [user], so A's summary lands in the user's chat. Empty = nobody
    // is waiting (wake-ups, manual runs).
    std::vector<ReplyFrame> reply_stack;
    bool expects_reply = true;  // false: the sender wants no answer back (information only)
    bool is_report = false;     // an asker is told the result of the work it asked for (not a request)
    bool name_chat = false;     // the answer may carry <title>..</title> that names the user's chat
    int hops = 0;               // how many agent-to-agent steps led to this job

    std::string ConversationId() const {
        return reply_stack.empty() ? std::string() : reply_stack.back().conversation_id;
    }

    std::string prompt;         // sent through stdin
    std::string system_prompt;  // kept short on purpose, see JobBuilder
    std::string model;          // empty = Claude Code default
    std::string tools;          // comma separated tools Claude may use; empty = none
    std::vector<std::string> mcp_configs;  // paths, passed with --mcp-config
    std::string resume_session;
    bool persist_session = false;
    int max_turns = 8;

    std::string vault_root;  // set by Enqueue: the vault the job belongs to

    int priority = 1;       // 0 low, 1 normal, 2 high
    int min_remaining = 0;  // skipped when less than this percent of usage is left
};

struct RunResult {
    RunJob job;
    std::string run_id;
    RunStatus status = RunStatus::Failed;
    std::string text;
    std::string session_id;
    std::string model;
    long tokens = 0;
    double cost_usd = 0;
    double duration_s = 0;
    time_t started = 0;  // 0 = not started
};

// What the page may show about a queued or running job.
struct JobInfo {
    long id = 0;
    bool running = false;
    std::string agent_id, agent_name, task_name, conversation_id;
    time_t started = 0;
    std::vector<std::string> activity;  // newest last: tools used and text written so far
};

// Queues jobs, runs at most max_concurrent of them at a time (highest priority
// first, never two jobs of the same agent together), applies the usage gate and reports
// results. Lives on the GUI thread.
class RunManager {
public:
    RunManager();
    ~RunManager();

    // Reads the output of the running processes and notices the ones that ended. The owner calls
    // this from a timer (about every 120 ms); it costs next to nothing while nothing runs.
    void Tick();

    // Where Claude Code is started and where run records are written.
    void SetVault(const std::string& root, const std::string& runs_dir);

    void Enqueue(RunJob job);
    void RefreshUsage();  // asks Claude Code for fresh usage numbers with a tiny request
    void CancelAll();
    // Stops one job (removes it from the queue or kills the process). False if it is gone already.
    bool CancelJob(long job_id);
    // Stops every queued or running job that still has to write into this conversation.
    void CancelConversation(const std::string& conversation_id);
    // A job that ended Failed, Skipped or Stopped can be queued again once, as it was.
    bool CanRetry(const std::string& run_id) const;
    bool Retry(const std::string& run_id);
    // Queued and running jobs (not the usage probe) with what they are doing.
    std::vector<JobInfo> Jobs() const;

    const UsageInfo& Usage() const { return usage_; }
    size_t QueuedCount() const { return queue_.size(); }
    size_t RunningCount() const { return active_.size(); }
    bool IsAgentBusy(const std::string& agent_id) const;
    // True while a queued or running request from agent `from` to agent `to` (a <send> that expects
    // an answer) has not finished: asking again would only repeat the same work.
    bool HasPendingRequest(const std::string& to, const std::string& from) const;
    // True while a queued or running job still has to write into this conversation.
    bool IsConversationActive(const std::string& conversation_id) const;
    // Agents whose queued or running job will write its answer into exactly this conversation.
    std::vector<std::string> ConversationWriters(const std::string& conversation_id) const;
    std::vector<std::string> BusyAgents() const;

    std::string default_model;  // used when a job names no model
    int max_concurrent = 1;  // a subscription has a shared limit, so keep this low

    std::function<void(const RunResult&)> on_finished;
    std::function<void()> on_changed;  // queue, running set or usage changed

private:
    struct Active;

    void Pump();
    void Start(RunJob job);
    void HandleLine(Active* a, const std::string& line);
    void Finish(Active* a, int exit_code);
    void Complete(RunResult result, bool save_record);
    bool UsageIsStale() const;
    bool ProbeActiveOrQueued() const;
    std::string BuildCommand(const RunJob& job, const std::string& system_file) const;
    void Changed();

    std::string root_;
    std::string runs_dir_;
    UsageInfo usage_;
    std::vector<RunJob> queue_;
    std::vector<std::unique_ptr<Active>> active_;
    long counter_ = 0;
    long next_job_id_ = 0;
    std::vector<std::pair<std::string, RunJob>> retryable_;  // newest last, run id -> job
};
