#include "Ui/web_frame.h"

#include "Core/platform.h"
#include "Core/run_stats.h"
#include "Core/text.h"
#include "Ui/telegram_client.h"
#include "Ui/web_json.h"

#include <dwmapi.h>

#include <algorithm>
#include <map>
#include <thread>

namespace {
constexpr int kRunTickMs = 120;
constexpr int kSchedulerTickMs = 15 * 1000;
constexpr UINT_PTR kRunTimerId = 1;
constexpr UINT_PTR kSchedulerTimerId = 2;
constexpr UINT kCallMessage = WM_APP + 1;
constexpr UINT kTelegramMessage = WM_APP + 2;
constexpr wchar_t kWindowClass[] = L"BurrowMainWindow";

constexpr COLORREF kBackground = RGB(15, 17, 23);  // the page's --bg, visible only while it loads

Json UsageWindowJson(const UsageWindow& w) {
    Json j = Json::MakeObject();
    j.Set("known", JBool(w.known));
    j.Set("used", JNum(w.used));
    j.Set("resets_at", JNum(static_cast<double>(w.resets_at)));
    return j;
}

UINT DpiOf(HWND hwnd) {
    // GetDpiForWindow needs Windows 10 1607; older systems just get 96.
    using Fn = UINT(WINAPI*)(HWND);
    static const Fn fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    const UINT dpi = fn ? fn(hwnd) : 96;
    return dpi ? dpi : 96;
}

int Scale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }
}  // namespace

WebFrame::WebFrame(VaultRegistry& registry, const Vault* vault, const std::string& start_page)
    : registry_(registry), start_page_(start_page) {
    if (vault)
        vault_ = *vault;
    runs_.on_changed = [this] { OnRunsChanged(); };
    runs_.on_finished = [this](const RunResult& r) { OnRunFinished(r); };
    telegram_ = TelegramConfig::Load(TelegramFile());
    if (HasVault())
        StartVaultMachinery();
}

WebFrame::~WebFrame() {
    runs_.on_changed = nullptr;
    runs_.on_finished = nullptr;
    if (poller_)
        poller_->stop = true;
    SetThreadExecutionState(ES_CONTINUOUS);
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

void WebFrame::StartVaultMachinery() {
    runs_.SetVault(vault_.Root(), vault_.SubDir("Runs"));
    store_ = std::make_unique<ConversationStore>(vault_.SubDir("Conversations"));
    router_ = std::make_unique<ConversationRouter>(store_.get(), &runs_);
    router_->on_telegram = [this](const AgentDefinition& from, const std::string& text) {
        if (!suppress_telegram_)
            SendTelegram(from.name + ": " + text);
    };
    ReloadAll();
}

// --- Window ---------------------------------------------------------------------------------

bool WebFrame::Create() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = &WebFrame::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kBackground);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, kWindowClass, L"Burrow", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                            CW_USEDEFAULT, 1280, 800, nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_)
        return false;

    // Size and place by the monitor's scale: 1280 x 800 device independent pixels, never larger than the screen.
    const UINT dpi = DpiOf(hwnd_);
    RECT work = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = std::min(Scale(1280, dpi), static_cast<int>(work.right - work.left) - 40);
    const int height = std::min(Scale(800, dpi), static_cast<int>(work.bottom - work.top) - 40);
    SetWindowPos(hwnd_, nullptr, work.left + (work.right - work.left - width) / 2,
                 work.top + (work.bottom - work.top - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);

    // Dark title bar in the colours of the page (Windows 11; older versions ignore these).
    const BOOL dark = TRUE;
    const COLORREF caption = RGB(22, 25, 34), caption_text = RGB(231, 233, 240);
    DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd_, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd_, 36 /*DWMWA_TEXT_COLOR*/, &caption_text, sizeof(caption_text));

    UpdateTitle();
    SetTimer(hwnd_, kRunTimerId, kRunTickMs, nullptr);
    RestartTelegramPoller();
    // The computer must stay awake while Burrow is open: scheduled wake-ups and Telegram messages only
    // work then. The screen may still turn off. Lasts until the window is destroyed (or the process ends).
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

    host_.on_message = [this](const std::string& body) {
        Post([this, body] { OnPageMessage(body); });  // leave the WebView2 callback first
    };
    std::wstring error;
    if (!host_.Create(hwnd_, error))
        ShowError(winstr::Narrow(error));
    RECT client = {};
    GetClientRect(hwnd_, &client);
    host_.Resize(client.right, client.bottom);

    if (HasVault())
        runs_.RefreshUsage();  // measure once at start so the gauges are not empty
    return true;
}

void WebFrame::Show() {
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

LRESULT CALLBACK WebFrame::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* frame = static_cast<WebFrame*>(create->lpCreateParams);
        frame->hwnd_ = hwnd;  // messages arrive during CreateWindowEx, before it returns
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(frame));
    }
    auto* self = reinterpret_cast<WebFrame*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCDESTROY || !self)
        return DefWindowProcW(hwnd, message, wparam, lparam);
    return self->HandleMessage(message, wparam, lparam);
}

LRESULT WebFrame::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_SIZE:
            host_.Resize(LOWORD(lparam), HIWORD(lparam));
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wparam) != WA_INACTIVE)
                host_.Focus();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            const UINT dpi = DpiOf(hwnd_);
            info->ptMinTrackSize.x = Scale(900, dpi);
            info->ptMinTrackSize.y = Scale(560, dpi);
            return 0;
        }
        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_TIMER:
            if (wparam == kRunTimerId)
                runs_.Tick();
            else if (wparam == kSchedulerTimerId)
                OnSchedulerTick();
            return 0;
        case kTelegramMessage:
            OnTelegramInbox();
            return 0;
        case kCallMessage: {
            std::unique_ptr<std::function<void()>> fn(reinterpret_cast<std::function<void()>*>(lparam));
            (*fn)();
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd_, kRunTimerId);
            KillTimer(hwnd_, kSchedulerTimerId);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void WebFrame::Post(std::function<void()> fn) {
    auto* boxed = new std::function<void()>(std::move(fn));
    if (!PostMessageW(hwnd_, kCallMessage, 0, reinterpret_cast<LPARAM>(boxed)))
        delete boxed;
}

void WebFrame::ShowError(const std::string& message) {
    MessageBoxW(hwnd_, winstr::Widen(message).c_str(), L"Burrow", MB_OK | MB_ICONERROR);
}

void WebFrame::UpdateTitle() {
    const std::string title = HasVault() ? "Burrow — " + vault_.Name() : std::string("Burrow");
    SetWindowTextW(hwnd_, winstr::Widen(title).c_str());
}

// --- Bridge ---------------------------------------------------------------------------------

void WebFrame::OnPageMessage(const std::string& body) {
    Json msg;
    if (!Json::Parse(body, msg) || !msg.IsObject())
        return;
    Json reply = Json::MakeObject();
    reply.Set("id", msg["id"]);
    try {
        reply.Set("data", Dispatch(msg["cmd"].AsString(), msg["args"]));
        reply.Set("ok", JBool(true));
    } catch (const std::exception& e) {
        reply = Json::MakeObject();
        reply.Set("id", msg["id"]);
        reply.Set("ok", JBool(false));
        reply.Set("error", JStr(std::string(e.what())));
    }
    host_.PostToPage(reply.Dump());
}

void WebFrame::Push(const std::string& event, Json data) {
    Json msg = Json::MakeObject();
    msg.Set("event", JStr(event));
    msg.Set("data", std::move(data));
    host_.PostToPage(msg.Dump());
}

void WebFrame::PushChanged() { Push("changed", Snapshot()); }

// --- State ----------------------------------------------------------------------------------

TeamContext WebFrame::Team() const {
    TeamContext team;
    team.agents = agents_;
    team.tasks = tasks_;
    team.skills = skills_;
    team.rules = rules_;
    team.projects = vault_.LoadProjects();  // fresh: agents may have edited notes since the last reload
    team.vault_root = vault_.Root();
    return team;
}

std::string WebFrame::NameOf(const std::string& agent_id) const {
    if (agent_id == "user")
        return "Sen";
    for (const AgentDefinition& a : agents_)
        if (a.id == agent_id)
            return a.name;
    return agent_id;
}

std::string WebFrame::ProjectName(const std::string& project_id) const {
    if (project_id.empty())
        return std::string();
    for (const ProjectDefinition& p : projects_)
        if (p.id == project_id)
            return p.name;
    return "(silinmiş proje)";
}

void WebFrame::ReloadRuns() { run_records_ = RunRecord::LoadAll(vault_.SubDir("Runs")); }

void WebFrame::ApplySettings() {
    runs_.max_concurrent = vault_.MaxConcurrent();
    runs_.default_model = vault_.DefaultModel();
    MaxAgentHops() = vault_.MaxHops();
}

void WebFrame::ReloadAll() {
    vault_.ReloadSettings();
    ApplySettings();
    UpdateTitle();
    auto by_name = [](const auto& a, const auto& b) { return str::CmpNoCase(a.name, b.name) < 0; };
    tasks_ = vault_.LoadTasks();
    std::sort(tasks_.begin(), tasks_.end(), by_name);
    skills_ = vault_.LoadSkills();
    std::sort(skills_.begin(), skills_.end(), by_name);
    rules_ = vault_.LoadRules();
    std::sort(rules_.begin(), rules_.end(), by_name);
    projects_ = vault_.LoadProjects();
    std::sort(projects_.begin(), projects_.end(), by_name);
    agents_ = OrderByHierarchy(vault_.LoadAgents());
    ReloadRuns();
}

Json WebFrame::Snapshot() const {
    const std::map<std::string, AgentUsage> usage = ComputeUsage(run_records_, timeutil::IsoDate(timeutil::Now()));
    const std::vector<std::string> busy = runs_.BusyAgents();
    Json out = Json::MakeObject();

    Json vault = Json::MakeObject();
    vault.Set("name", JStr(vault_.Name()));
    vault.Set("root", JStr(vault_.Root()));
    vault.Set("language", JStr(vault_.Language()));
    vault.Set("max_concurrent", JNum(vault_.MaxConcurrent()));
    vault.Set("max_hops", JNum(vault_.MaxHops()));
    vault.Set("default_model", JStr(vault_.DefaultModel()));
    vault.Set("daily_budget", JNum(static_cast<double>(vault_.DailyTokenBudget())));
    vault.Set("weekly_budget", JNum(static_cast<double>(vault_.WeeklyTokenBudget())));
    out.Set("vault", vault);

    // Tokens of all agents together, counted against the vault-wide budget.
    long tokens_today = 0, tokens_7d = 0;
    for (const auto& entry : usage) {
        tokens_today += entry.second.tokens_today;
        tokens_7d += entry.second.tokens_7d;
    }
    Json budget = Json::MakeObject();
    budget.Set("tokens_today", JNum(static_cast<double>(tokens_today)));
    budget.Set("tokens_7d", JNum(static_cast<double>(tokens_7d)));
    budget.Set("daily_over", JBool(OverDailyLimit(tokens_today, static_cast<int>(vault_.DailyTokenBudget()))));
    budget.Set("weekly_over", JBool(OverDailyLimit(tokens_7d, static_cast<int>(vault_.WeeklyTokenBudget()))));
    out.Set("budget", budget);

    Json tg = Json::MakeObject();  // the token itself never goes to the page
    tg.Set("token_set", JBool(!telegram_.token.empty()));
    tg.Set("chat_id", JStr(telegram_.chat_id));
    tg.Set("notify_failures", JBool(telegram_.notify_failures));
    tg.Set("ready", JBool(telegram_.Ready()));
    tg.Set("receive", JBool(telegram_.receive));
    tg.Set("agent_id", JStr(telegram_.agent_id));
    std::string receive_error;
    if (poller_) {
        std::lock_guard<std::mutex> lock(poller_->mutex);
        receive_error = poller_->error;
    }
    tg.Set("receive_error", JStr(receive_error));
    {
        std::lock_guard<std::mutex> lock(telegram_status_->mutex);
        tg.Set("last_error", JStr(telegram_status_->last_error));
    }
    out.Set("telegram", tg);

    Json jobs = Json::MakeArray();
    for (const JobInfo& i : runs_.Jobs()) {
        Json jj = Json::MakeObject();
        jj.Set("id", JNum(static_cast<double>(i.id)));
        jj.Set("running", JBool(i.running));
        jj.Set("agent_id", JStr(i.agent_id));
        jj.Set("agent", JStr(i.agent_name));
        jj.Set("task", JStr(i.task_name));
        jj.Set("conversation", JStr(i.conversation_id));
        jj.Set("started", JNum(static_cast<double>(i.started)));
        Json lines = Json::MakeArray();
        for (const std::string& l : i.activity)
            lines.Push(JStr(l));
        jj.Set("activity", lines);
        jobs.Push(jj);
    }
    out.Set("jobs", jobs);
    out.Set("needs_vault", JBool(!HasVault()));
    out.Set("start_page", JStr(start_page_));
    out.Set("scheduler", JBool(scheduler_on_));

    const UsageInfo& u = runs_.Usage();
    Json usage_json = Json::MakeObject();
    usage_json.Set("known", JBool(u.Known()));
    usage_json.Set("five_hour", UsageWindowJson(u.five_hour));
    usage_json.Set("seven_day", UsageWindowJson(u.seven_day));
    usage_json.Set("remaining", JNum(u.RemainingPercent()));
    usage_json.Set("updated", JNum(static_cast<double>(u.updated)));
    out.Set("usage", usage_json);

    Json queue = Json::MakeObject();
    queue.Set("running", JNum(static_cast<double>(runs_.RunningCount())));
    queue.Set("queued", JNum(static_cast<double>(runs_.QueuedCount())));
    out.Set("queue", queue);

    Json agents = Json::MakeArray();
    for (const AgentDefinition& a : agents_) {
        const auto it = usage.find(a.id);
        const AgentUsage au = it == usage.end() ? AgentUsage() : it->second;
        const bool working = std::find(busy.begin(), busy.end(), a.id) != busy.end();
        const char* state = !a.enabled ? "sleeping"
                            : working  ? "working"
                            : OverDailyLimit(au.tokens_today, a.daily_token_limit) ? "limit"
                                                                                     : "idle";
        Json j = Json::MakeObject();
        j.Set("id", JStr(a.id));
        j.Set("name", JStr(a.name));
        j.Set("role", JStr(a.role));
        Json mascot = Json::MakeObject();
        mascot.Set("species", JNum(a.mascot_species));
        mascot.Set("color", JNum(a.mascot_color));
        mascot.Set("accessory", JNum(a.mascot_accessory));
        j.Set("mascot", mascot);
        j.Set("reports_to", JStr(a.reports_to));
        j.Set("model", JStr(a.model));
        j.Set("enabled", JBool(a.enabled));
        j.Set("daily_tokens", JNum(a.daily_token_limit));
        Json skills = Json::MakeArray();
        for (const std::string& s : a.skills)
            skills.Push(JStr(s));
        j.Set("skills", skills);
        Json wakeups = Json::MakeArray();
        for (const Wakeup& w : a.wakeups) {
            Json wj = Json::MakeObject();
            wj.Set("time", JStr(w.time));
            wj.Set("interval", JNum(w.interval_minutes));
            wj.Set("task", JStr(w.task_id));
            wj.Set("project", JStr(w.project_id));
            wj.Set("text", JStr(w.Describe()));
            wakeups.Push(wj);
        }
        j.Set("wakeups", wakeups);
        j.Set("persona", JStr(a.persona));
        j.Set("state", JStr(std::string(state)));
        j.Set("tokens_today", JNum(static_cast<double>(au.tokens_today)));
        j.Set("tokens_7d", JNum(static_cast<double>(au.tokens_7d)));
        j.Set("runs_7d", JNum(au.runs_7d));
        j.Set("failures_7d", JNum(au.failures_7d));
        j.Set("last_run", JStr(au.last_run));
        agents.Push(j);
    }
    out.Set("agents", agents);

    Json tasks = Json::MakeArray();
    for (const TaskDefinition& t : tasks_) {
        Json j = Json::MakeObject();
        j.Set("id", JStr(t.id));
        j.Set("name", JStr(t.name));
        j.Set("model", JStr(t.model));
        j.Set("priority", JNum(t.priority));
        j.Set("min_remaining", JNum(t.min_remaining_percent));
        j.Set("body", JStr(t.body));
        Json users = Json::MakeArray();
        for (const AgentDefinition& a : agents_)
            for (const Wakeup& w : a.wakeups)
                if (w.task_id == t.id) {
                    users.Push(JStr(a.name));
                    break;
                }
        j.Set("used_by", users);
        tasks.Push(j);
    }
    out.Set("tasks", tasks);

    Json skills = Json::MakeArray();
    for (const SkillDefinition& s : skills_) {
        Json j = Json::MakeObject();
        j.Set("id", JStr(s.id));
        j.Set("name", JStr(s.name));
        j.Set("description", JStr(s.description));
        j.Set("tools", JStr(s.tools));
        j.Set("mcp_config", JStr(s.mcp_config));
        j.Set("body", JStr(s.body));
        Json users = Json::MakeArray();
        for (const AgentDefinition& a : agents_)
            if (std::find(a.skills.begin(), a.skills.end(), s.id) != a.skills.end())
                users.Push(JStr(a.name));
        j.Set("used_by", users);
        skills.Push(j);
    }
    out.Set("skills", skills);

    Json rules = Json::MakeArray();
    int rule_tokens = 0;
    for (const RuleDefinition& r : rules_) {
        Json j = Json::MakeObject();
        j.Set("id", JStr(r.id));
        j.Set("name", JStr(r.name));
        j.Set("enabled", JBool(r.enabled));
        j.Set("body", JStr(r.body));
        j.Set("tokens", JNum(r.EstimatedTokens()));
        if (r.enabled)
            rule_tokens += r.EstimatedTokens();
        rules.Push(j);
    }
    out.Set("rules", rules);
    out.Set("rule_tokens", JNum(rule_tokens));

    Json projects = Json::MakeArray();
    int note_count = 0;
    for (const ProjectDefinition& p : projects_) {
        Json j = Json::MakeObject();
        j.Set("id", JStr(p.id));
        j.Set("name", JStr(p.name));
        j.Set("summary", JStr(p.summary));
        j.Set("tokens", JNum(p.EstimatedTokens()));
        Json notes = Json::MakeArray();
        for (const ProjectNote& n : p.notes) {
            Json nj = Json::MakeObject();
            nj.Set("id", JStr(n.id));
            nj.Set("name", JStr(n.name));
            nj.Set("enabled", JBool(n.enabled));
            nj.Set("body", JStr(n.body));
            nj.Set("tokens", JNum(n.EstimatedTokens()));
            notes.Push(nj);
        }
        note_count += static_cast<int>(p.notes.size());
        j.Set("notes", notes);
        projects.Push(j);
    }
    out.Set("projects", projects);
    out.Set("note_count", JNum(note_count));

    Json recent = Json::MakeArray();  // the usage probe has no agent and is not interesting here
    int shown = 0;
    for (const RunRecord& r : run_records_) {
        if (r.agent_id.empty())
            continue;
        Json j = Json::MakeObject();
        j.Set("id", JStr(r.id));
        j.Set("time", JStr(r.time));
        j.Set("agent", JStr(r.agent_name));
        j.Set("task", JStr(r.task_name));
        j.Set("source", JStr(r.source));
        j.Set("status", JStr(r.status));
        j.Set("tokens", JNum(static_cast<double>(r.tokens)));
        j.Set("duration", JNum(r.duration_s));
        recent.Push(j);
        if (++shown >= 15)
            break;
    }
    out.Set("recent_runs", recent);
    return out;
}

// --- Runs -----------------------------------------------------------------------------------

void WebFrame::OnRunsChanged() { PushChanged(); }

void WebFrame::OnRunFinished(const RunResult& r) {
    const RunJob& job = r.job;
    if (!HasVault() || job.vault_root != vault_.Root()) {
        if (HasVault())
            ReloadRuns();
        PushChanged();
        return;  // started in another vault, only its run record matters
    }
    ReloadRuns();
    // An agent with a write tool may have changed agents, rules or skills (setup skills): show them.
    if (str::Contains(job.tools, "Write") || str::Contains(job.tools, "Edit"))
        ReloadAll();
    if (telegram_.notify_failures && job.source == JobSource::Scheduled && r.status == RunStatus::Failed)
        SendTelegram("⚠ " + job.agent_name + (job.task_name.empty() ? std::string() : " / " + job.task_name) +
                     " başarısız oldu: " + (str::Utf8Length(r.text) > 300 ? str::Utf8Left(r.text, 300) + "…" : r.text));
    // An answer headed for Telegram already is the message; its own <telegram> blocks would arrive twice.
    const bool answers_telegram = !job.reply_stack.empty() && job.reply_stack.back().agent_id == "user" &&
                                  telegram_pending_.count(job.reply_stack.back().conversation_id) > 0;
    suppress_telegram_ = answers_telegram;
    router_->HandleResult(Team(), r);
    suppress_telegram_ = false;
    // Every answer written into a chat that Telegram started goes back there, including the one an agent
    // gives while it still waits for a colleague. The chat stays "pending" until no job needs it any more.
    if (!job.reply_stack.empty() && job.reply_stack.back().agent_id == "user" &&
        telegram_pending_.count(job.reply_stack.back().conversation_id) > 0) {
        // Send exactly what the router wrote into the chat, so Burrow and Telegram never differ.
        ConversationInfo info;
        std::vector<ConvMessage> written;
        std::string answer;
        if (store_->Load(job.reply_stack.back().conversation_id, info, written) && !written.empty() &&
            written.back().from == job.agent_id && written.back().to == "user")
            answer = written.back().text;
        SendTelegram(answer.empty() ? std::string("(boş cevap)") : answer);
        if (!runs_.IsConversationActive(job.reply_stack.back().conversation_id))
            telegram_pending_.erase(job.reply_stack.back().conversation_id);
    }
    PushChanged();
}

std::string WebFrame::TelegramFile() const { return fsx::Join(fsx::UserDataDir(), "telegram.ini"); }

void WebFrame::SendTelegram(const std::string& text) {
    if (!telegram_.Ready())
        return;
    // The thread owns copies, so it stays safe even if the window closes while it waits for the network.
    std::thread([config = telegram_, text, status = telegram_status_] {
        const std::string error = telegram::Send(config, text);
        std::lock_guard<std::mutex> lock(status->mutex);
        status->last_error = error;
        status->sent_any = status->sent_any || error.empty();
    }).detach();
}

void WebFrame::RestartTelegramPoller() {
    if (poller_)
        poller_->stop = true;  // the old thread notices within one poll and ends
    poller_.reset();
    if (!telegram_.Ready() || !telegram_.receive)
        return;
    poller_ = std::make_shared<TelegramPoller>();
    std::thread([poller = poller_, token = telegram_.token, hwnd = hwnd_] {
        long long offset = -1;  // the first call only learns where the newest message is: no backlog
        std::vector<telegram::Incoming> messages;
        while (!poller->stop) {
            std::string error;
            long long next = offset;
            const bool ok = telegram::GetUpdates(token, offset, offset < 0 ? 0 : 8, messages, next, error);
            if (poller->stop)
                break;
            {
                std::lock_guard<std::mutex> lock(poller->mutex);
                poller->error = ok ? std::string() : error;
                for (const telegram::Incoming& m : messages)
                    poller->inbox.push_back(m);
            }
            if (ok) {
                offset = next < 0 ? 0 : next;
                if (!messages.empty())
                    PostMessageW(hwnd, kTelegramMessage, 0, 0);
            } else {
                for (int waited = 0; waited < 5000 && !poller->stop; waited += 100)
                    Sleep(100);  // a failed poll (offline, conflict) is retried in a few seconds
            }
        }
    }).detach();
}

void WebFrame::OnTelegramInbox() {
    if (!poller_)
        return;
    std::vector<telegram::Incoming> inbox;
    {
        std::lock_guard<std::mutex> lock(poller_->mutex);
        inbox.swap(poller_->inbox);
    }
    for (const telegram::Incoming& m : inbox)
        if (m.chat_id == telegram_.chat_id)  // only the chat set up in Settings may talk to the agents
            HandleTelegramText(m.text);
}

// One message from the chat: it goes to an agent's own "Telegram" chat (made on first use), and the
// answer is sent back to Telegram when the job ends (OnRunFinished).
void WebFrame::HandleTelegramText(const std::string& raw) {
    std::string text = str::Trim(raw);
    if (text.empty() || !HasVault())
        return;
    std::string agent_id = telegram_.agent_id;
    if (text[0] == '@') {  // "@ajan_id mesaj" talks to another agent
        const std::string wanted = str::BeforeFirst(text.substr(1), ' ');
        for (const AgentDefinition& a : agents_)
            if (a.id == wanted) {
                agent_id = wanted;
                text = str::Trim(text.substr(1 + wanted.size()));
            }
    }
    const TeamContext team = Team();
    const AgentDefinition* agent = team.FindAgent(agent_id);
    if (!agent || !agent->enabled) {
        SendTelegram("Bu mesajı alacak ajan seçilmemiş ya da uykuda. Burrow Ayarlar sayfasından bir ajan seç; ya da mesaja @ajan_id ile başla.");
        return;
    }
    if (text.empty())
        return;

    std::string conversation;
    for (const ConversationInfo& c : store_->List())
        if (c.kind == "chat" && !c.participants.empty() && c.participants[0] == agent->id &&
            str::StartsWith(c.title, "Telegram")) {
            conversation = c.id;
            break;
        }
    if (conversation.empty())
        conversation = store_->Create("chat", "Telegram · " + agent->name, {agent->id});
    if (runs_.IsConversationActive(conversation)) {
        SendTelegram("Önceki mesajın hâlâ işleniyor, bitince tekrar yaz.");
        return;
    }
    const std::string command = str::Lower(text);
    if (command == "/yeni" || command == "/new" || command == "/temizle" || command == "/clear") {
        // Forget the conversation: the next message starts a fresh Claude session (cheaper, too).
        // The transcript stays in Burrow, with a line that marks the break.
        store_->SetSession(conversation, std::string());
        store_->Append(conversation, {std::string(), std::string(), std::string(), "(Telegram: yeni konuşma başlatıldı, önceki mesajlar unutuldu.)"});
        SendTelegram("Yeni konuşma başladı, önceki mesajları unuttum.");
        PushChanged();
        return;
    }
    ConversationInfo info;
    std::vector<ConvMessage> messages;
    store_->Load(conversation, info, messages);
    store_->Append(conversation, {std::string(), "user", agent->id, text});
    // The whole answer goes to Telegram as it is, so the agent needs no Telegram skill text for this job.
    AgentDefinition replying = *agent;
    replying.skills.erase(std::remove(replying.skills.begin(), replying.skills.end(), std::string(kTelegramSkillId)),
                          replying.skills.end());
    RunJob job = MakeChatJob(team, replying, conversation, info.session_id, text, info.project, false);
    job.prompt += "\n\n(Bu mesaj Telegram'dan geldi; cevabın aynen Telegram'a iletilecek, kısa tut.)";
    telegram_pending_.insert(conversation);
    runs_.Enqueue(job);
    PushChanged();
}

void WebFrame::SetScheduler(bool on) {
    scheduler_on_ = on;
    if (on) {
        scheduler_.Reset(timeutil::Now());
        SetTimer(hwnd_, kSchedulerTimerId, kSchedulerTickMs, nullptr);
    } else {
        KillTimer(hwnd_, kSchedulerTimerId);
    }
}

void WebFrame::OnSchedulerTick() {
    if (!HasVault())
        return;
    const TeamContext team = Team();
    const time_t now = timeutil::Now();
    const std::map<std::string, AgentUsage> usage = ComputeUsage(run_records_, timeutil::IsoDate(now));
    long tokens_today = 0, tokens_7d = 0;
    for (const auto& entry : usage) {
        tokens_today += entry.second.tokens_today;
        tokens_7d += entry.second.tokens_7d;
    }
    // The vault-wide budget only holds back wake-ups; chats and manual runs are never blocked.
    const bool over_budget = OverDailyLimit(tokens_today, static_cast<int>(vault_.DailyTokenBudget())) ||
                             OverDailyLimit(tokens_7d, static_cast<int>(vault_.WeeklyTokenBudget()));
    for (const Scheduler::Due& due : scheduler_.Collect(agents_, now)) {
        const AgentDefinition* agent = team.FindAgent(due.agent_id);
        if (!agent || over_budget || runs_.IsAgentBusy(agent->id))
            continue;  // over the vault budget, or one thing at a time per agent
        const auto spent = usage.find(agent->id);
        if (spent != usage.end() && OverDailyLimit(spent->second.tokens_today, agent->daily_token_limit))
            continue;  // daily token limit reached, wakes up again tomorrow
        const TaskDefinition* task = due.wakeup.task_id.empty() ? nullptr : team.FindTask(due.wakeup.task_id);
        if (!due.wakeup.task_id.empty() && !task)
            continue;  // the task was deleted
        if (!due.wakeup.project_id.empty() && !team.FindProject(due.wakeup.project_id))
            continue;  // the project was deleted
        runs_.Enqueue(MakeTaskJob(team, *agent, task, JobSource::Scheduled, due.wakeup.project_id));
    }
}
