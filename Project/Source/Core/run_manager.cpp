#include "Core/run_manager.h"

#include "Core/id_util.h"
#include "Core/json.h"
#include "Core/platform.h"
#include "Core/run_record.h"
#include "Core/text.h"

#include <algorithm>
#include <ctime>
#include <set>

namespace {
constexpr time_t kUsageStaleSeconds = 10 * 60;

std::string Quote(const std::string& s) {
    std::string out = str::ReplaceAll(s, "\"", "\\\"");
    while (!out.empty() && out.back() == '\\')
        out.pop_back();
    return "\"" + out + "\"";
}

// "Bash(git *)" -> "Bash"; MCP tools are not part of the built-in --tools list.
std::string BuiltinName(const std::string& tool) {
    if (str::StartsWith(tool, "mcp__"))
        return std::string();
    return str::BeforeFirst(tool, '(');
}
}  // namespace

std::string JobSourceLabel(JobSource s) {
    switch (s) {
        case JobSource::Chat: return "sohbet";
        case JobSource::Manual: return "elle";
        case JobSource::Scheduled: return "zamanlı";
        case JobSource::Agent: return "ajan";
        case JobSource::Probe: return "kullanım";
    }
    return std::string();
}

std::string RunStatusLabel(RunStatus s) {
    switch (s) {
        case RunStatus::Done: return "tamamlandı";
        case RunStatus::Failed: return "hata";
        case RunStatus::Skipped: return "atlandı";
        case RunStatus::Stopped: return "durduruldu";
    }
    return std::string();
}

struct RunManager::Active {
    RunJob job;
    std::unique_ptr<ClaudeProcess> process;
    std::string system_file;
    time_t started = 0;
    std::chrono::steady_clock::time_point started_at;
    bool cancelled = false;
    bool got_result = false;
    RunResult result;
};

RunManager::RunManager() = default;

RunManager::~RunManager() {
    for (auto& a : active_) {
        if (a->process) {
            a->process->on_line = nullptr;
            a->process->on_exit = nullptr;
            a->process->Stop();
        }
        fsx::RemoveFile(a->system_file);
    }
}

void RunManager::SetVault(const std::string& root, const std::string& runs_dir) {
    root_ = root;
    runs_dir_ = runs_dir;
}

void RunManager::Changed() {
    if (on_changed)
        on_changed();
}

bool RunManager::IsAgentBusy(const std::string& agent_id) const {
    for (const auto& a : active_)
        if (a->job.agent_id == agent_id)
            return true;
    return std::any_of(queue_.begin(), queue_.end(),
                       [&](const RunJob& j) { return j.agent_id == agent_id; });
}

bool RunManager::IsConversationActive(const std::string& conversation_id) const {
    auto involves = [&](const RunJob& job) {
        return std::any_of(job.reply_stack.begin(), job.reply_stack.end(),
                           [&](const ReplyFrame& f) { return f.conversation_id == conversation_id; });
    };
    for (const auto& a : active_)
        if (involves(a->job))
            return true;
    return std::any_of(queue_.begin(), queue_.end(), involves);
}

std::vector<std::string> RunManager::BusyAgents() const {
    std::vector<std::string> ids;
    for (const auto& a : active_)
        if (a->job.source != JobSource::Probe)
            ids.push_back(a->job.agent_id);
    return ids;
}

bool RunManager::UsageIsStale() const {
    return !usage_.Known() || std::time(nullptr) - usage_.updated > kUsageStaleSeconds;
}

bool RunManager::ProbeActiveOrQueued() const {
    for (const auto& a : active_)
        if (a->job.source == JobSource::Probe)
            return true;
    return std::any_of(queue_.begin(), queue_.end(),
                       [](const RunJob& j) { return j.source == JobSource::Probe; });
}

void RunManager::RefreshUsage() {
    if (ProbeActiveOrQueued())
        return;
    RunJob probe;
    probe.source = JobSource::Probe;
    probe.agent_name = "Kullanım ölçümü";
    probe.prompt = "ok";
    probe.system_prompt = "Reply with: ok";
    probe.model = "haiku";
    probe.max_turns = 1;
    probe.priority = 3;  // ahead of everything else
    Enqueue(probe);
}

void RunManager::Enqueue(RunJob job) {
    job.vault_root = root_;
    queue_.push_back(std::move(job));
    Changed();
    Pump();
}

void RunManager::CancelAll() {
    std::vector<RunJob> dropped;
    dropped.swap(queue_);
    for (RunJob& j : dropped) {
        if (j.source == JobSource::Probe)
            continue;
        RunResult r;
        r.job = j;
        r.status = RunStatus::Stopped;
        r.text = "Sıradayken durduruldu.";
        Complete(r, true);
    }
    for (auto& a : active_) {
        a->cancelled = true;
        if (a->process)
            a->process->Stop();
    }
    Changed();
}

// Picks the next job: highest priority first, oldest first within a priority.
void RunManager::Pump() {
    while (static_cast<int>(active_.size()) < max_concurrent && !queue_.empty()) {
        std::stable_sort(queue_.begin(), queue_.end(),
                         [](const RunJob& a, const RunJob& b) { return a.priority > b.priority; });

        size_t pick = queue_.size();
        bool waiting_for_usage = false;
        for (size_t i = 0; i < queue_.size(); ++i) {
            const RunJob& j = queue_[i];
            if (j.min_remaining <= 0 || j.source == JobSource::Probe) {
                pick = i;
                break;
            }
            if (UsageIsStale()) {  // we cannot judge the gate yet, measure first
                waiting_for_usage = true;
                break;
            }
            if (usage_.RemainingPercent() >= j.min_remaining) {
                pick = i;
                break;
            }
            RunResult r;  // not enough usage left: skip the job and say so
            r.job = j;
            r.status = RunStatus::Skipped;
            r.text = str::Format("Kalan kullanım %%%.0f, bu görev için en az %%%d gerekiyor.",
                                  usage_.RemainingPercent(), j.min_remaining);
            queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(i));
            Complete(r, true);
            i = static_cast<size_t>(-1);  // restart the scan, the vector changed
            continue;
        }
        if (waiting_for_usage) {
            if (!ProbeActiveOrQueued()) {
                RefreshUsage();
                return;  // RefreshUsage enqueued a probe and pumped again
            }
            // A probe is already queued behind others; let it come to the front.
            auto probe = std::find_if(queue_.begin(), queue_.end(),
                                      [](const RunJob& j) { return j.source == JobSource::Probe; });
            if (probe == queue_.end())
                return;  // the probe is running; its result pumps again
            pick = static_cast<size_t>(probe - queue_.begin());
        }
        if (pick >= queue_.size())
            break;
        RunJob job = std::move(queue_[pick]);
        queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(pick));
        Start(std::move(job));
    }
    Changed();
}

std::string RunManager::BuildCommand(const RunJob& job, const std::string& system_file) const {
    std::string cmd = "claude -p --output-format stream-json --verbose --permission-prompts none "
                      "--strict-mcp-config --disable-slash-commands --setting-sources project";

    std::set<std::string> builtin;
    std::string allowed;
    for (const std::string& raw : str::Split(job.tools, ',')) {
        const std::string tool = str::Trim(raw);
        if (tool.empty())
            continue;
        allowed += (allowed.empty() ? "" : ",") + tool;
        const std::string name = BuiltinName(tool);
        if (!name.empty())
            builtin.insert(name);
    }
    std::string builtin_list;
    for (const std::string& name : builtin)
        builtin_list += (builtin_list.empty() ? "" : ",") + name;
    cmd += " --tools " + Quote(builtin_list);  // "" turns every built-in tool off
    // Claude in Chrome is not an MCP config file: only this flag loads its tools, so it is
    // switched on just for jobs whose skills ask for one of them.
    if (str::Contains(allowed, "mcp__claude-in-chrome"))
        cmd += " --chrome";
    if (!allowed.empty())
        cmd += " --allowedTools " + Quote(allowed);

    if (!job.model.empty())
        cmd += " --model " + Quote(job.model);
    if (job.max_turns > 0)
        cmd += " --max-turns " + std::to_string(job.max_turns);
    for (const std::string& config : job.mcp_configs)
        cmd += " --mcp-config " + Quote(config);
    cmd += " --system-prompt-file " + Quote(system_file);
    if (!job.resume_session.empty())
        cmd += " --resume " + Quote(job.resume_session);
    else if (!job.persist_session)
        cmd += " --no-session-persistence";
    return cmd;
}

void RunManager::Start(RunJob job) {
    auto active = std::make_unique<Active>();
    Active* a = active.get();
    a->job = job;
    a->started = timeutil::Now();
    a->started_at = std::chrono::steady_clock::now();
    a->result.job = job;
    a->result.started = a->started;

    fsx::MakeDirs(runs_dir_);
    a->system_file = fsx::Join(runs_dir_, str::Format(".system_%ld.tmp", ++counter_));
    fsx::WriteFile(a->system_file, job.system_prompt);

    a->process = std::make_unique<ClaudeProcess>();
    a->process->on_line = [this, a](const std::string& line) { HandleLine(a, line); };
    a->process->on_exit = [this, a](int code) { Finish(a, code); };

    active_.push_back(std::move(active));
    if (!a->process->Start(BuildCommand(job, a->system_file), root_, job.prompt)) {
        RunResult r = a->result;
        r.status = RunStatus::Failed;
        r.text = "Claude Code başlatılamadı. 'claude' komutu PATH içinde mi?";
        fsx::RemoveFile(a->system_file);
        active_.erase(std::remove_if(active_.begin(), active_.end(),
                                     [a](const std::unique_ptr<Active>& p) { return p.get() == a; }),
                      active_.end());
        Complete(r, job.source != JobSource::Probe);
        return;
    }
    Changed();
}

void RunManager::Tick() {
    if (active_.empty())
        return;
    // Finish() erases entries (and can start new ones), so poll over a copy of the pointers and
    // check that each one is still active before touching it.
    std::vector<Active*> snapshot;
    for (const auto& a : active_)
        snapshot.push_back(a.get());
    for (Active* a : snapshot) {
        const bool still_active = std::any_of(active_.begin(), active_.end(),
                                              [a](const std::unique_ptr<Active>& p) { return p.get() == a; });
        if (still_active && a->process)
            a->process->Poll();
    }
}

void RunManager::HandleLine(Active* a, const std::string& line) {
    Json j;
    if (!Json::Parse(line, j))
        return;
    if (usage_.UpdateFromEvent(j, std::time(nullptr))) {
        Changed();
        return;
    }
    const std::string type = j["type"].AsString();
    if (type == "system" && j["subtype"].AsString() == "init") {
        a->result.session_id = j["session_id"].AsString();
        a->result.model = j["model"].AsString();
    } else if (type == "result") {
        a->got_result = true;
        RunResult& r = a->result;
        r.text = j["result"].AsString();
        if (!j["session_id"].IsNull())
            r.session_id = j["session_id"].AsString();
        r.cost_usd = j["total_cost_usd"].AsNumber();
        const Json& u = j["usage"];
        r.tokens = static_cast<long>(u["input_tokens"].AsNumber() + u["output_tokens"].AsNumber() +
                                     u["cache_creation_input_tokens"].AsNumber() +
                                     u["cache_read_input_tokens"].AsNumber());
        r.status = j["is_error"].AsBool() ? RunStatus::Failed : RunStatus::Done;
        if (r.status == RunStatus::Failed && r.text.empty())
            r.text = j["subtype"].AsString();
    }
}

void RunManager::Finish(Active* a, int exit_code) {
    RunResult r = a->result;
    r.job = a->job;
    r.started = a->started;
    r.duration_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - a->started_at).count();
    if (a->cancelled) {
        r.status = RunStatus::Stopped;
        r.text = "Kullanıcı durdurdu.";
    } else if (!a->got_result) {
        r.status = RunStatus::Failed;
        r.text = str::Format("Claude Code sonuç vermeden bitti (kod %d).", exit_code);
    }
    const bool save = a->job.source != JobSource::Probe;
    fsx::RemoveFile(a->system_file);
    active_.erase(std::remove_if(active_.begin(), active_.end(),
                                 [a](const std::unique_ptr<Active>& p) { return p.get() == a; }),
                  active_.end());  // destroys *a and its process; the process does not touch itself afterwards
    Complete(r, save);
    Pump();
}

void RunManager::Complete(RunResult result, bool save_record) {
    if (result.model.empty())
        result.model = result.job.model;
    if (result.started == 0)
        result.started = timeutil::Now();
    result.run_id = str::Format("%s_%s_%ld", timeutil::Format(result.started, "%Y%m%d_%H%M%S").c_str(),
                                 MakeId(result.job.agent_id.empty() ? std::string("sistem") : result.job.agent_id).c_str(),
                                 ++counter_);
    if (save_record) {
        RunRecord rec;
        rec.id = result.run_id;
        rec.time = timeutil::Format(result.started, "%Y-%m-%d %H:%M:%S");
        rec.agent_id = result.job.agent_id;
        rec.agent_name = result.job.agent_name;
        rec.task_name = result.job.task_name;
        rec.source = JobSourceLabel(result.job.source);
        rec.status = RunStatusLabel(result.status);
        rec.model = result.model;
        rec.tokens = result.tokens;
        rec.cost_usd = result.cost_usd;
        rec.duration_s = result.duration_s;
        rec.conversation_id = result.job.ConversationId();
        rec.text = result.text;
        // A job that outlives a vault switch is recorded in the vault it was started in.
        const std::string dir =
            result.job.vault_root.empty() ? runs_dir_ : fsx::Join(result.job.vault_root, "Runs");
        fsx::MakeDirs(dir);
        rec.Save(dir);
    }
    if (on_finished && result.job.source != JobSource::Probe)
        on_finished(result);
    Changed();
}
