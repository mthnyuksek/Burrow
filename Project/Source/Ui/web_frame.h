#pragma once

#include "Core/agent_definition.h"
#include "Core/conversation.h"
#include "Core/conversation_router.h"
#include "Core/job_builder.h"
#include "Core/json.h"
#include "Core/run_manager.h"
#include "Core/run_record.h"
#include "Core/scheduler.h"
#include "Core/skill_definition.h"
#include "Core/task_definition.h"
#include "Core/telegram_config.h"
#include "Core/vault.h"
#include "Core/vault_registry.h"
#include "Ui/telegram_client.h"
#include "Ui/web_view_host.h"

#include <windows.h>

#include <atomic>
#include <functional>
#include <set>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

// A mistake the user can fix (empty name, ...): its text is shown in the page.
struct ApiError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The main window: an HTML page (the Web folder, compiled into the program) inside WebView2.
// This class owns the vault data and the run machinery; the page only shows it and sends commands.
// Without a vault (first start) the page shows the vault picker until one is opened.
//
// Protocol (JSON text in both directions, see Web/api.js):
//   page -> program  {"id":7,"cmd":"chat.send","args":{...}}   answered by  {"id":7,"ok":true,"data":...}
//                                                                         or {"id":7,"ok":false,"error":"..."}
//   program -> page  {"event":"changed","data":<snapshot>}
class WebFrame {
public:
    // vault: the vault to open, or null to start without one. start_page may be empty.
    WebFrame(VaultRegistry& registry, const Vault* vault, const std::string& start_page);
    ~WebFrame();

    bool Create();  // registers the window class, creates the window and the web view
    void Show();

private:
    // Window (web_frame.cpp).
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void Post(std::function<void()> fn);  // runs fn from the message loop, outside the current callback
    void ShowError(const std::string& text);
    void UpdateTitle();

    // Bridge (web_frame.cpp).
    void OnPageMessage(const std::string& text);
    void Push(const std::string& event, Json data);
    void PushChanged();

    // State.
    bool HasVault() const { return !vault_.Root().empty(); }
    void StartVaultMachinery();  // run manager, conversation store and router for vault_
    void ReloadAll();
    void ApplySettings();  // vault.ini settings -> run manager and router limits
    void ReloadRuns();
    TeamContext Team() const;
    std::string NameOf(const std::string& agent_id) const;
    std::string ProjectName(const std::string& project_id) const;
    Json Snapshot() const;

    void OnRunsChanged();
    void OnRunFinished(const RunResult& result);
    void OnSchedulerTick();
    // Sends text to the user's Telegram chat from a worker thread; the outcome shows in the snapshot.
    void SendTelegram(const std::string& text);
    std::string TelegramFile() const;
    // Reading what the chat writes to the bot: a worker thread long-polls and posts kTelegramMessage.
    void RestartTelegramPoller();  // after a settings change, and once the window exists
    void OnTelegramInbox();        // UI thread: hands each message to an agent's Telegram chat
    void HandleTelegramText(const std::string& text);
    void SetScheduler(bool on);

    // Commands (web_api.cpp). Throws ApiError for mistakes of the user.
    Json Dispatch(const std::string& cmd, const Json& args);
    Json ChatList() const;
    Json ChatLoad(const std::string& id) const;
    Json ChatExport(const std::string& id) const;
    std::string ChatNew(const std::string& agent_id, const std::string& project_id);
    void ChatSend(const std::string& id, const std::string& text);
    Json RunList() const;
    void RunNow(const std::string& agent_id, const std::string& task_id, const std::string& project_id);
    void SaveAgent(const Json& j);
    void DeleteAgent(const std::string& id);
    void SaveTask(const Json& j);
    void SaveSkill(const Json& j);
    void SaveRule(const Json& j);
    void SaveProject(const Json& j);
    void SaveNote(const Json& j);

    // Vaults (web_api.cpp).
    Json VaultList() const;
    Json VaultBrowse(const std::string& title);
    void VaultOpen(const std::string& path);
    void VaultCreate(const std::string& name, const std::string& path);
    void VaultAdd(const std::string& path);

    HWND hwnd_ = nullptr;
    VaultRegistry& registry_;
    Vault vault_;  // empty (no root) until a vault is opened
    std::vector<AgentDefinition> agents_;  // in hierarchy order
    std::vector<TaskDefinition> tasks_;
    std::vector<SkillDefinition> skills_;
    std::vector<RuleDefinition> rules_;
    std::vector<ProjectDefinition> projects_;
    std::vector<RunRecord> run_records_;  // newest first, reloaded whenever a run ends

    RunManager runs_;
    std::unique_ptr<ConversationStore> store_;
    std::unique_ptr<ConversationRouter> router_;
    Scheduler scheduler_;
    TelegramConfig telegram_;  // %APPDATA%urrow	elegram.ini, loaded at start
    struct TelegramStatus {
        std::mutex mutex;
        std::string last_error;  // of the latest background send, empty if it went through
        bool sent_any = false;
    };
    std::shared_ptr<TelegramStatus> telegram_status_ = std::make_shared<TelegramStatus>();
    struct TelegramPoller {
        std::atomic<bool> stop{false};
        std::mutex mutex;
        std::vector<telegram::Incoming> inbox;
        std::string error;  // last polling problem, empty while it works
    };
    std::shared_ptr<TelegramPoller> poller_;
    bool suppress_telegram_ = false;  // while the router handles an answer that goes to Telegram anyway
    std::set<std::string> telegram_pending_;  // chats with a Telegram message in work: the answer goes back
    bool scheduler_on_ = false;

    WebViewHost host_;
    std::string start_page_;
};
