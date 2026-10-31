// Commands the page can send to the program (see the protocol in web_frame.h).
#include "Core/id_util.h"
#include "Core/platform.h"
#include "Core/skill_templates.h"
#include "Core/text.h"
#include "Ui/folder_dialog.h"
#include "Ui/remote_server.h"
#include "Ui/telegram_client.h"
#include "Ui/web_frame.h"
#include "Ui/web_json.h"

#include <algorithm>

namespace {
Json IdJson(const std::string& id) {
    Json j = Json::MakeObject();
    j.Set("id", JStr(id));
    return j;
}

Json SkillJson(const SkillDefinition& s) {
    Json j = Json::MakeObject();
    j.Set("id", JStr(s.id));
    j.Set("name", JStr(s.name));
    j.Set("description", JStr(s.description));
    j.Set("tools", JStr(s.tools));
    j.Set("mcp_config", JStr(s.mcp_config));
    j.Set("body", JStr(s.body));
    return j;
}

std::string Trimmed(const Json& j) { return str::Trim(WStr(j)); }

void Require(bool ok, const char* message) {
    if (!ok)
        throw ApiError(message);
}
}  // namespace

Json WebFrame::Dispatch(const std::string& cmd, const Json& args) {
    if (cmd == "bootstrap")
        return Snapshot();
    // Everything else needs an open vault, except the vault commands themselves.
    Require(HasVault() || cmd.rfind("vault.", 0) == 0, "Önce bir kasa seç.");

    // Chats and runs.
    if (cmd == "chats.list")
        return ChatList();
    if (cmd == "chat.load")
        return ChatLoad(WStr(args["id"]));
    if (cmd == "chat.new")
        return IdJson(ChatNew(WStr(args["agent"]), WStr(args["project"])));
    if (cmd == "chat.send") {
        ChatSend(WStr(args["id"]), WStr(args["text"]));
        return Json();
    }
    if (cmd == "chat.rename") {
        const std::string title = Trimmed(args["title"]);
        Require(!title.empty(), "Başlık boş olamaz.");
        store_->Rename(WStr(args["id"]), title);
        return Json();
    }
    if (cmd == "chat.delete") {
        store_->Delete(WStr(args["id"]));
        return Json();
    }
    if (cmd == "chat.export")
        return ChatExport(WStr(args["id"]));
    if (cmd == "runs.list")
        return RunList();
    if (cmd == "run.get") {
        for (const RunRecord& r : run_records_)
            if (r.id == WStr(args["id"])) {
                Json j = Json::MakeObject();
                j.Set("text", JStr(r.text));
                return j;
            }
        throw ApiError("Kayıt bulunamadı.");
    }
    if (cmd == "run.now") {
        RunNow(WStr(args["agent"]), WStr(args["task"]), WStr(args["project"]));
        return Json();
    }
    if (cmd == "usage.refresh") {
        runs_.RefreshUsage();
        return Json();
    }
    if (cmd == "job.cancel") {
        runs_.CancelJob(static_cast<long>(args["id"].AsNumber()));
        return Json();
    }
    if (cmd == "chat.stop") {
        runs_.CancelConversation(WStr(args["id"]));
        return Json();
    }
    if (cmd == "run.retry") {
        Require(runs_.Retry(WStr(args["id"])), "Bu iş artık yeniden denenemez (uygulama kapanıp açılmış olabilir).");
        return Json();
    }
    if (cmd == "runs.stopAll") {
        runs_.CancelAll();
        return Json();
    }
    if (cmd == "scheduler.set") {
        SetScheduler(args["on"].AsBool());
        PushChanged();
        return Json();
    }

    // Vaults.
    if (cmd == "vault.list")
        return VaultList();
    if (cmd == "vault.browse")
        return VaultBrowse(WStr(args["title"]));
    if (cmd == "vault.open") {
        VaultOpen(WStr(args["path"]));
        return Json();
    }
    if (cmd == "vault.create") {
        VaultCreate(Trimmed(args["name"]), WStr(args["path"]));
        return Json();
    }
    if (cmd == "vault.add") {
        VaultAdd(WStr(args["path"]));
        return Json();
    }
    if (cmd == "vault.remove") {
        registry_.Remove(WStr(args["path"]));  // the folder itself is left untouched
        return Json();
    }

    // Friends joining over the internet (owner only: RemoteRoleAllows keeps "remote." from them).
    if (cmd == "remote.set") {
        SetRemote(args["on"].AsBool());
        return Json();
    }
    if (cmd == "remote.install") {
        tunnel_.Install();
        PushChanged();
        return Json();
    }
    if (cmd == "remote.invite") {
        const std::string name = Trimmed(args["name"]);
        const std::string role = WStr(args["role"]);
        Require(!name.empty(), "Davet için bir isim yaz.");
        Require(role == "viewer" || role == "editor", "Rol geçersiz.");
        RemoteInvite invite;
        invite.name = name;
        invite.role = role;
        invite.token = RemoteServer::NewToken();
        invite.id = RemoteServer::NewToken().substr(0, 8);
        remote_config_.invites.push_back(invite);
        Require(remote_config_.Save(RemoteFile()), "Davet kaydedilemedi.");
        remote_.SetInvites(remote_config_.invites);
        PushChanged();
        return IdJson(invite.id);
    }
    if (cmd == "remote.revoke") {
        auto& list = remote_config_.invites;
        const std::string id = WStr(args["id"]);
        list.erase(std::remove_if(list.begin(), list.end(), [&](const RemoteInvite& i) { return i.id == id; }),
                   list.end());
        remote_config_.Save(RemoteFile());
        remote_.SetInvites(list);  // the friend is locked out from the next call on
        PushChanged();
        return Json();
    }
    if (cmd == "remote.link") {
        const RemoteInvite* invite = remote_config_.Find(WStr(args["id"]));
        Require(invite != nullptr, "Davet bulunamadı.");
        Json j = Json::MakeObject();
        j.Set("link", JStr(InviteLink(*invite)));
        return j;
    }

    if (cmd == "telegram.set") {  // an empty token keeps the saved one
        TelegramConfig next = telegram_;
        if (!Trimmed(args["token"]).empty())
            next.token = Trimmed(args["token"]);
        if (!args["chat_id"].IsNull())
            next.chat_id = Trimmed(args["chat_id"]);
        if (!args["notify_failures"].IsNull())
            next.notify_failures = args["notify_failures"].AsBool();
        if (!args["receive"].IsNull())
            next.receive = args["receive"].AsBool();
        if (!args["agent_id"].IsNull())
            next.agent_id = Trimmed(args["agent_id"]);
        Require(next.Save(TelegramFile()), "Telegram ayarları kaydedilemedi.");
        telegram_ = next;
        RestartTelegramPoller();
        PushChanged();
        return Json();
    }
    if (cmd == "telegram.detect") {  // uses the typed token if there is one, else the saved one
        std::string error;
        const std::string token = Trimmed(args["token"]).empty() ? telegram_.token : Trimmed(args["token"]);
        if (poller_)
            poller_->stop = true;  // two readers of the same bot would get in each other's way
        const std::string id = telegram::DetectChatId(token, error);
        RestartTelegramPoller();
        Require(!id.empty(), error.c_str());
        return IdJson(id);
    }
    if (cmd == "telegram.test") {
        TelegramConfig test = telegram_;
        if (!Trimmed(args["token"]).empty())
            test.token = Trimmed(args["token"]);
        if (!Trimmed(args["chat_id"]).empty())
            test.chat_id = Trimmed(args["chat_id"]);
        const std::string error = telegram::Send(test, "Burrow: test mesajı. Telegram bağlantısı çalışıyor ✓");
        Require(error.empty(), error.c_str());
        return Json();
    }
    if (cmd == "settings.set") {
        std::string error;
        auto put = [&](const char* arg, const char* key, std::string value) {
            if (!args[arg].IsNull() && !vault_.SetSetting(key, value, error))
                throw ApiError(error);
        };
        if (!args["language"].IsNull() && !vault_.SetLanguage(WStr(args["language"]), error))
            throw ApiError(error);
        auto count = [&](const char* arg, int lo, int hi) {
            return std::to_string(std::clamp(static_cast<int>(args[arg].AsNumber()), lo, hi));
        };
        put("max_concurrent", "MaxConcurrent", count("max_concurrent", 1, 4));
        put("max_hops", "MaxHops", count("max_hops", 1, 20));
        put("default_model", "DefaultModel", Trimmed(args["default_model"]));
        put("daily_budget", "DailyTokens", count("daily_budget", 0, 2000000000));
        put("weekly_budget", "WeeklyTokens", count("weekly_budget", 0, 2000000000));
        ApplySettings();
        PushChanged();
        return Json();
    }

    // Agents, tasks, skills, rules, projects.
    if (cmd == "agent.save") {
        SaveAgent(args);
        return Json();
    }
    if (cmd == "agent.delete") {
        DeleteAgent(WStr(args["id"]));
        return Json();
    }
    if (cmd == "task.save") {
        SaveTask(args);
        return Json();
    }
    if (cmd == "task.delete") {
        for (const TaskDefinition& t : tasks_)
            if (t.id == WStr(args["id"])) {
                std::string error;
                Require(vault_.DeleteTask(t, error), "Görev silinemedi.");
                break;
            }
        ReloadAll();
        PushChanged();
        return Json();
    }
    if (cmd == "skill.save") {
        SaveSkill(args);
        return Json();
    }
    if (cmd == "skill.delete") {
        for (const SkillDefinition& s : skills_)
            if (s.id == WStr(args["id"])) {
                std::string error;
                Require(vault_.DeleteSkill(s, error), "Yetenek silinemedi.");
                break;
            }
        ReloadAll();
        PushChanged();
        return Json();
    }
    if (cmd == "templates.list") {
        Json list = Json::MakeArray();
        for (const SkillTemplate& t : SkillTemplates())
            list.Push(SkillJson(LocalizedSkill(t, vault_.Language())));
        return list;
    }
    if (cmd == "rule.save") {
        SaveRule(args);
        return Json();
    }
    if (cmd == "rule.delete") {
        for (const RuleDefinition& r : rules_)
            if (r.id == WStr(args["id"])) {
                std::string error;
                Require(vault_.DeleteRule(r, error), "Kural silinemedi.");
                break;
            }
        ReloadAll();
        PushChanged();
        return Json();
    }
    if (cmd == "project.save") {
        SaveProject(args);
        return Json();
    }
    if (cmd == "project.delete") {
        for (const ProjectDefinition& p : projects_)
            if (p.id == WStr(args["id"])) {
                std::string error;
                Require(vault_.DeleteProject(p, error), "Proje silinemedi.");
                break;
            }
        ReloadAll();
        PushChanged();
        return Json();
    }
    if (cmd == "note.save") {
        SaveNote(args);
        return Json();
    }
    if (cmd == "note.delete") {
        for (const ProjectDefinition& p : projects_)
            if (p.id == WStr(args["project"]))
                for (const ProjectNote& n : p.notes)
                    if (n.id == WStr(args["id"])) {
                        std::string error;
                        Require(vault_.DeleteNote(p.id, n, error), "Not silinemedi.");
                    }
        ReloadAll();
        PushChanged();
        return Json();
    }
    throw ApiError("Bilinmeyen komut: " + cmd);
}

// --- Chats ----------------------------------------------------------------------------------

Json WebFrame::ChatList() const {
    Json list = Json::MakeArray();
    for (const ConversationInfo& c : store_->List()) {
        Json j = Json::MakeObject();
        j.Set("id", JStr(c.id));
        j.Set("kind", JStr(c.kind));
        j.Set("title", JStr(c.title));
        j.Set("project", JStr(c.project));
        j.Set("project_name", JStr(ProjectName(c.project)));
        j.Set("updated", JStr(c.updated));
        Json people = Json::MakeArray();
        bool active = c.kind == "chat" ? runs_.IsConversationActive(c.id)
                                       : !runs_.ConversationWriters(c.id).empty();
        for (const std::string& p : c.participants) {
            Json pj = Json::MakeObject();
            pj.Set("id", JStr(p));
            pj.Set("name", JStr(NameOf(p)));
            people.Push(pj);
        }
        j.Set("participants", people);
        j.Set("active", JBool(active));
        Json writer_ids = Json::MakeArray();
        for (const std::string& w : runs_.ConversationWriters(c.id))
            writer_ids.Push(JStr(w));
        j.Set("writers", writer_ids);
        list.Push(j);
    }
    return list;
}

Json WebFrame::ChatLoad(const std::string& id) const {
    ConversationInfo info;
    std::vector<ConvMessage> messages;
    if (!store_->Load(id, info, messages))
        throw ApiError("Sohbet bulunamadı.");
    Json out = Json::MakeObject();
    out.Set("id", JStr(info.id));
    out.Set("kind", JStr(info.kind));
    out.Set("title", JStr(info.title));
    out.Set("project", JStr(info.project));
    out.Set("project_name", JStr(ProjectName(info.project)));
    Json people = Json::MakeArray();
    for (const std::string& p : info.participants) {
        Json pj = Json::MakeObject();
        pj.Set("id", JStr(p));
        pj.Set("name", JStr(NameOf(p)));
        people.Push(pj);
    }
    out.Set("participants", people);

    Json msgs = Json::MakeArray();
    for (const ConvMessage& m : messages) {
        Json mj = Json::MakeObject();
        mj.Set("time", JStr(m.time));
        mj.Set("from", JStr(m.from));
        mj.Set("from_name", JStr(NameOf(m.from)));
        mj.Set("to", JStr(m.to));
        mj.Set("text", JStr(m.text));
        msgs.Push(mj);
    }
    out.Set("messages", msgs);

    // "writers" = agents currently composing a message for this very conversation.
    Json writers = Json::MakeArray();
    for (const std::string& w : runs_.ConversationWriters(id)) {
        Json wj = Json::MakeObject();
        wj.Set("id", JStr(w));
        wj.Set("name", JStr(NameOf(w)));
        writers.Push(wj);
    }
    out.Set("writers", writers);
    const bool busy = info.kind == "chat" ? runs_.IsConversationActive(id)
                                          : !runs_.ConversationWriters(id).empty();
    out.Set("busy", JBool(busy));
    out.Set("writable", JBool(info.kind == "chat" && !busy));
    return out;
}

Json WebFrame::ChatExport(const std::string& id) const {
    ConversationInfo info;
    std::vector<ConvMessage> messages;
    if (!store_->Load(id, info, messages))
        throw ApiError("Sohbet bulunamadı.");
    std::string markdown = "# " + info.title + "\n\n";
    for (const ConvMessage& m : messages)
        markdown += "**" + NameOf(m.from) + "** · " + m.time + "\n\n" + m.text + "\n\n";
    Json out = Json::MakeObject();
    out.Set("filename", JStr(MakeId(info.title) + ".md"));
    out.Set("markdown", JStr(markdown));
    return out;
}

std::string WebFrame::ChatNew(const std::string& agent_id, const std::string& project_id) {
    const AgentDefinition* agent = nullptr;
    for (const AgentDefinition& a : agents_)
        if (a.id == agent_id)
            agent = &a;
    Require(agent != nullptr, "Ajan bulunamadı.");
    const std::string title = "Sohbet " + timeutil::Format(timeutil::Now(), "%d.%m %H:%M");
    return store_->Create("chat", title, {agent->id}, project_id);
}

void WebFrame::ChatSend(const std::string& id, const std::string& text) {
    if (str::Trim(text).empty())
        return;
    ConversationInfo info;
    std::vector<ConvMessage> messages;
    if (!store_->Load(id, info, messages) || info.participants.empty())
        throw ApiError("Sohbet bulunamadı.");
    const TeamContext team = Team();
    const AgentDefinition* agent = team.FindAgent(info.participants[0]);
    Require(agent != nullptr, "Bu sohbetin ajanı silinmiş.");
    store_->Append(id, {std::string(), "user", agent->id, text});
    runs_.Enqueue(MakeChatJob(team, *agent, id, info.session_id, text, info.project, messages.empty()));
    PushChanged();  // after Enqueue, so the chat already counts as active
}

Json WebFrame::RunList() const {
    Json list = Json::MakeArray();
    int shown = 0;
    for (const RunRecord& r : run_records_) {
        Json j = Json::MakeObject();
        j.Set("id", JStr(r.id));
        j.Set("time", JStr(r.time));
        j.Set("agent", JStr(r.agent_name));
        j.Set("task", JStr(r.task_name));
        j.Set("source", JStr(r.source));
        j.Set("status", JStr(r.status));
        j.Set("model", JStr(r.model));
        j.Set("tokens", JNum(static_cast<double>(r.tokens)));
        j.Set("cost", JNum(r.cost_usd));
        j.Set("duration", JNum(r.duration_s));
        j.Set("retry", JBool(runs_.CanRetry(r.id)));
        list.Push(j);
        if (++shown >= 300)
            break;
    }
    return list;
}

void WebFrame::RunNow(const std::string& agent_id, const std::string& task_id, const std::string& project_id) {
    const TeamContext team = Team();
    const AgentDefinition* agent = team.FindAgent(agent_id);
    Require(agent != nullptr, "Ajan bulunamadı.");
    const TaskDefinition* task = task_id.empty() ? nullptr : team.FindTask(task_id);
    Require(task_id.empty() || task != nullptr, "Görev bulunamadı.");
    runs_.Enqueue(MakeTaskJob(team, *agent, task, JobSource::Manual, project_id));
}

// --- Saving ---------------------------------------------------------------------------------

void WebFrame::SaveAgent(const Json& j) {
    AgentDefinition a;
    a.id = WStr(j["id"]);
    a.name = Trimmed(j["name"]);
    Require(!a.name.empty(), "Ajan adı boş olamaz.");
    a.role = Trimmed(j["role"]);
    a.mascot_species = WInt(j["mascot"]["species"]);
    a.mascot_color = WInt(j["mascot"]["color"]);
    a.mascot_accessory = WInt(j["mascot"]["accessory"]);
    a.reports_to = WStr(j["reports_to"]);
    Require(a.reports_to.empty() || a.reports_to != a.id, "Ajan kendi yöneticisi olamaz.");
    Require(a.id.empty() || a.reports_to.empty() || !WouldCreateCycle(agents_, a.id, a.reports_to),
            "Bu yönetici seçimi bir yönetim döngüsü oluşturur.");
    a.model = Trimmed(j["model"]);
    a.enabled = j["enabled"].AsBool(true);
    a.daily_token_limit = std::max(0, WInt(j["daily_tokens"]));
    for (size_t i = 0; i < j["skills"].Size(); ++i)
        a.skills.push_back(WStr(j["skills"].At(i)));
    for (size_t i = 0; i < j["wakeups"].Size(); ++i) {
        const Json& wj = j["wakeups"].At(i);
        Wakeup w;
        w.time = Trimmed(wj["time"]);
        w.interval_minutes = std::max(0, WInt(wj["interval"]));
        w.task_id = WStr(wj["task"]);
        w.project_id = WStr(wj["project"]);
        w.date = w.interval_minutes > 0 ? std::string() : Trimmed(wj["date"]);
        Require(w.date.empty() || Wakeup::IsValidDate(w.date), "Uyanış tarihi geçersiz.");
        w.days = w.interval_minutes > 0 || !w.date.empty() ? 0 : std::clamp(WInt(wj["days"]), 0, 127);
        if (w.days == 127)
            w.days = 0;
        Require(w.interval_minutes > 0 || Wakeup::IsValidTime(w.time), "Uyanış saati SS:DD biçiminde olmalı.");
        if (w.interval_minutes > 0)
            w.time.clear();
        a.wakeups.push_back(w);
    }
    a.persona = WStr(j["persona"]);
    std::string error;
    Require(vault_.SaveAgent(a, error), "Ajan kaydedilemedi.");
    ReloadAll();
    PushChanged();
}

void WebFrame::DeleteAgent(const std::string& id) {
    const AgentDefinition* victim = nullptr;
    for (const AgentDefinition& a : agents_)
        if (a.id == id)
            victim = &a;
    Require(victim != nullptr, "Ajan bulunamadı.");
    std::string error;
    Require(vault_.DeleteAgent(*victim, error), "Ajan silinemedi.");
    for (AgentDefinition other : agents_)
        if (other.reports_to == id) {
            other.reports_to.clear();
            vault_.SaveAgent(other, error);
        }
    ReloadAll();
    PushChanged();
}

void WebFrame::SaveTask(const Json& j) {
    TaskDefinition t;
    t.id = WStr(j["id"]);
    t.name = Trimmed(j["name"]);
    Require(!t.name.empty(), "Görev adı boş olamaz.");
    t.model = Trimmed(j["model"]);
    t.priority = std::clamp(WInt(j["priority"], kPriorityNormal), 0, 2);
    t.min_remaining_percent = std::clamp(WInt(j["min_remaining"]), 0, 100);
    t.body = WStr(j["body"]);
    std::string error;
    Require(vault_.SaveTask(t, error), "Görev kaydedilemedi.");
    ReloadAll();
    PushChanged();
}

void WebFrame::SaveSkill(const Json& j) {
    SkillDefinition s;
    s.id = WStr(j["id"]);
    s.name = Trimmed(j["name"]);
    Require(!s.name.empty(), "Yetenek adı boş olamaz.");
    s.description = Trimmed(j["description"]);
    s.tools = Trimmed(j["tools"]);
    s.mcp_config = Trimmed(j["mcp_config"]);
    s.body = WStr(j["body"]);
    std::string error;
    Require(vault_.SaveSkill(s, error), "Yetenek kaydedilemedi.");
    ReloadAll();
    PushChanged();
}

void WebFrame::SaveRule(const Json& j) {
    RuleDefinition r;
    r.id = WStr(j["id"]);
    r.name = Trimmed(j["name"]);
    Require(!r.name.empty(), "Kural adı boş olamaz.");
    r.enabled = j["enabled"].AsBool(true);
    r.body = WStr(j["body"]);
    std::string error;
    Require(vault_.SaveRule(r, error), "Kural kaydedilemedi.");
    ReloadAll();
    PushChanged();
}

void WebFrame::SaveProject(const Json& j) {
    ProjectDefinition p;
    p.id = WStr(j["id"]);
    p.name = Trimmed(j["name"]);
    Require(!p.name.empty(), "Proje adı boş olamaz.");
    p.summary = WStr(j["summary"]);
    std::string error;
    Require(vault_.SaveProject(p, error), "Proje kaydedilemedi.");
    ReloadAll();
    PushChanged();
}

void WebFrame::SaveNote(const Json& j) {
    ProjectNote n;
    n.id = WStr(j["id"]);
    n.name = Trimmed(j["name"]);
    Require(!n.name.empty(), "Not adı boş olamaz.");
    n.enabled = j["enabled"].AsBool(true);
    n.body = WStr(j["body"]);
    std::string error;
    Require(vault_.SaveNote(WStr(j["project"]), n, error), "Not kaydedilemedi.");
    ReloadAll();
    PushChanged();
}

// --- Vaults ---------------------------------------------------------------------------------

Json WebFrame::VaultList() const {
    Json out = Json::MakeObject();
    Json entries = Json::MakeArray();
    for (const VaultRegistry::Entry& e : registry_.Entries()) {
        Json j = Json::MakeObject();
        j.Set("name", JStr(e.name));
        j.Set("path", JStr(e.path));
        entries.Push(j);
    }
    out.Set("entries", entries);
    out.Set("current", JStr(vault_.Root()));
    return out;
}

Json WebFrame::VaultBrowse(const std::string& title) {
    std::string path;
    Json out = Json::MakeObject();
    out.Set("path", JStr(PickFolder(hwnd_, title, path) ? path : std::string()));
    return out;
}

void WebFrame::VaultOpen(const std::string& path) {
    Vault next;
    std::string error;
    if (!Vault::Open(path, next, error))
        throw ApiError(error);
    if (HasVault() && runs_.RunningCount() + runs_.QueuedCount() > 0)
        runs_.CancelAll();  // the page has asked the person already
    vault_ = next;
    registry_.SetLastOpened(vault_.Root());
    StartVaultMachinery();
    Push("vault", Json());  // the page forgets what it had open
    PushChanged();
    if (!runs_.Usage().Known())
        runs_.RefreshUsage();
}

void WebFrame::VaultCreate(const std::string& name, const std::string& path) {
    std::string error;
    if (!Vault::Create(name, path, error))
        throw ApiError(error);
    registry_.Add(name, path);
    VaultOpen(path);
}

void WebFrame::VaultAdd(const std::string& path) {
    Vault vault;
    std::string error;
    if (!Vault::Open(path, vault, error))
        throw ApiError(error);
    registry_.Add(vault.Name(), vault.Root());
}
