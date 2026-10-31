// Self test of the UI independent core. `core_selftest` runs the offline checks;
// `core_selftest --live` additionally starts Claude Code with a few tiny Haiku
// requests (usage gate, runner, agent-to-agent conversation).
#include "Core/agent_definition.h"
#include "Core/conversation.h"
#include "Core/conversation_router.h"
#include "Core/id_util.h"
#include "Core/job_builder.h"
#include "Core/json.h"
#include "Core/markdown_file.h"
#include "Core/platform.h"
#include "Core/remote_config.h"
#include "Core/run_manager.h"
#include "Core/run_record.h"
#include "Core/run_stats.h"
#include "Core/scheduler.h"
#include "Core/skill_templates.h"
#include "Core/telegram_config.h"
#include "Core/text.h"
#include "Core/usage_info.h"
#include "Core/vault.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>

namespace {
int g_failures = 0;
int g_passes = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (cond) {                                                          \
            ++g_passes;                                                      \
        } else {                                                             \
            ++g_failures;                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

void Say(const std::string& s) { std::printf("%s\n", s.c_str()); }

bool Has(const std::string& s, const char* part) { return str::Contains(s, part); }
bool Starts(const std::string& s, const char* prefix) { return str::StartsWith(s, prefix); }

std::string TempPath(const char* name) { return fsx::Join(fsx::TempDir(), name); }

// A fresh, empty folder under the temp directory.
std::string FreshDir(const char* name) {
    const std::string dir = TempPath(name);
    fsx::RemoveDirRecursive(dir);
    return dir;
}

// --- offline checks -----------------------------------------------------------------------

void TestText() {
    CHECK(str::Trim("  a b \r\n") == "a b" && str::Trim("   ").empty());
    CHECK(str::Split("a,b,,c", ',').size() == 4 && str::Split("", ',').empty());
    CHECK(str::ReplaceAll("a\"b\"", "\"", "\\\"") == "a\\\"b\\\"");
    long n = 0;
    CHECK(str::ToLong("42", n) && n == 42 && !str::ToLong("4x", n) && !str::ToLong("", n));
    CHECK(str::Utf8Length("çş1") == 3 && str::Utf8Left("çşü", 2) == "çş");
    CHECK(str::Format("%d-%s", 7, "x") == "7-x");
    CHECK(str::CmpNoCase("ABC", "abc") == 0 && str::CmpNoCase("Çay", "çay") == 0 && str::CmpNoCase("a", "b") < 0);
    CHECK(timeutil::DateDaysBefore("2026-03-02", 6) == "2026-02-24");
}

void TestJson() {
    Json j;
    CHECK(Json::Parse(R"({"a":[1,2.5,true,null],"s":"x\n\"y\" \u00e7\ud83d\ude00","o":{"k":"v"}})", j));
    CHECK(j["a"].Size() == 4 && j["a"].At(1).AsNumber() == 2.5 && j["a"].At(2).AsBool());
    CHECK(j["o"]["k"].AsString() == "v");
    CHECK(j["missing"]["deeper"].IsNull());
    CHECK(j["s"].AsString() == "x\n\"y\" \xC3\xA7\xF0\x9F\x98\x80");
    Json again;
    CHECK(Json::Parse(j.Dump(), again) && again["s"].AsString() == j["s"].AsString());
    CHECK(!Json::Parse("{\"a\":", j));
    CHECK(!Json::Parse("[1,2", j));
}

void TestUsage() {
    const std::string line =
        R"({"type":"rate_limit_event","rate_limit_info":{"status":"allowed","resetsAt":1793217600,"rateLimitType":"five_hour","unifiedWindows":{"five_hour":{"utilization":0.08,"resetsAt":1793217600,"status":"allowed"},"seven_day":{"utilization":0.04,"resetsAt":1793779200,"status":"allowed"}}}})";
    Json j;
    CHECK(Json::Parse(line, j));
    UsageInfo usage;
    CHECK(!usage.Known() && usage.RemainingPercent() < 0);
    CHECK(usage.UpdateFromEvent(j, 1000));
    CHECK(usage.Known() && usage.updated == 1000);
    CHECK(static_cast<int>(usage.RemainingPercent() + 0.5) == 92);  // the tighter window
    Json other;
    CHECK(Json::Parse(R"({"type":"assistant"})", other) && !usage.UpdateFromEvent(other, 2000));
}

void TestMarkdownAndAgents() {
    MarkdownDoc doc;
    doc.Add("name", "A");
    doc.Add("wake", "09:00 | x");
    doc.Add("wake", "every 30 | ");
    doc.body = "Gövde çş\nikinci satır";
    MarkdownDoc back;
    CHECK(MarkdownDoc::Parse(doc.Serialize(), back));
    CHECK(back.GetAll("wake").size() == 2 && back.Get("name") == "A" && Starts(back.body, "Gövde"));

    AgentDefinition a;
    a.id = "ali";
    a.name = "Ali Veli";
    a.role = "Araştırmacı";
    a.mascot_species = 2;
    a.mascot_color = 5;
    a.mascot_accessory = 3;
    a.reports_to = "patron";
    a.model = "haiku";
    a.skills = {"wa", "web"};
    a.wakeups = {{"09:00", 0, "gunluk", ""}, {"", 30, "", ""}};
    a.persona = "Kısa yaz.";
    AgentDefinition b;
    CHECK(AgentDefinition::Parse("ali", a.Serialize(), b));
    CHECK(b.name == a.name && b.role == a.role && b.mascot_species == 2 && b.mascot_color == 5 &&
          b.mascot_accessory == 3 && b.reports_to == "patron" && b.model == "haiku");
    CHECK(b.skills.size() == 2 && b.skills[1] == "web");
    CHECK(b.wakeups.size() == 2 && b.wakeups[0].time == "09:00" && b.wakeups[0].task_id == "gunluk" &&
          b.wakeups[1].interval_minutes == 30 && b.wakeups[1].task_id.empty());

    // The first file format must still load.
    AgentDefinition legacy;
    CHECK(AgentDefinition::Parse("eski", "---\nname: Eski\nmodel: opus\nenabled: false\nwake_times: 09:00, 18:30\n"
                                         "interval_minutes: 15\n---\nEski talimat\n", legacy));
    CHECK(legacy.model == "opus" && !legacy.enabled && legacy.wakeups.size() == 3 &&
          legacy.wakeups[1].time == "18:30" && legacy.wakeups[2].interval_minutes == 15);
    CHECK(Starts(legacy.persona, "Eski talimat"));

    CHECK(MakeId("Araştırmacı Ajan #1") == "arastirmaci_ajan_1");
    CHECK(MakeId("İş ÇĞÖŞÜ") == "is_cgosu" && MakeId("###") == "item");
}

void TestHierarchy() {
    AgentDefinition boss, mid, low;
    boss.id = "boss"; boss.name = "Boss";
    mid.id = "mid"; mid.name = "Mid"; mid.reports_to = "boss";
    low.id = "low"; low.name = "Low"; low.reports_to = "mid";
    const std::vector<AgentDefinition> all = {low, boss, mid};
    CHECK(WouldCreateCycle(all, "boss", "low"));   // boss -> low -> mid -> boss
    CHECK(!WouldCreateCycle(all, "low", "boss"));
    CHECK(WouldCreateCycle(all, "mid", "mid"));
    const auto ordered = OrderByHierarchy(all);
    CHECK(ordered.size() == 3 && ordered[0].id == "boss" && ordered[1].id == "mid" && ordered[2].id == "low");
}

void TestSkillTemplates() {
    const std::string dir = FreshDir("burrow_selftest_templates");
    std::string error;
    CHECK(Vault::Create("Sablon", dir, error));
    Vault vault;
    CHECK(Vault::Open(dir, vault, error));

    // A new vault comes with exactly the default setup skills, and they parse back intact.
    size_t defaults = 0;
    for (const SkillTemplate& t : SkillTemplates())
        defaults += t.installed_by_default ? 1 : 0;
    const std::vector<SkillDefinition> skills = vault.LoadSkills();
    CHECK(defaults == 4 && skills.size() == defaults);
    for (const SkillDefinition& s : skills) {
        CHECK(!s.name.empty() && !s.tools.empty() && !s.body.empty());
        CHECK(Has(s.tools, "Edit"));
    }
    fsx::RemoveDirRecursive(dir);
}

void TestVaultLanguage() {
    const std::string dir = FreshDir("burrow_selftest_language");
    std::string error;
    CHECK(Vault::Create("Dil", dir, error));
    Vault vault;
    CHECK(Vault::Open(dir, vault, error));
    CHECK(vault.Language() == "tr" && vault.Name() == "Dil");

    CHECK(vault.SetLanguage("en", error));
    Vault again;
    CHECK(Vault::Open(dir, again, error));
    CHECK(again.Language() == "en" && again.Name() == "Dil");  // name and version survive the rewrite
    CHECK(Vault::NormalizeLanguage("xx") == "tr" && Vault::NormalizeLanguage(" EN ") == "en");

    // A vault.ini of an older version has no Language line: Turkish, and SetLanguage appends it.
    CHECK(fsx::WriteFile(fsx::Join(dir, "vault.ini"), "[Vault]\nName=Eski\nVersion=2\n"));
    CHECK(Vault::Open(dir, again, error) && again.Language() == "tr" && again.Name() == "Eski");
    CHECK(again.SetLanguage("en", error));
    again.ReloadSettings();
    CHECK(again.Language() == "en" && again.Name() == "Eski");

    // Run limits and the token budget: defaults, then values (clamped) written next to the rest.
    CHECK(again.MaxConcurrent() == 1 && again.MaxHops() == 8 && again.DefaultModel().empty());
    CHECK(again.DailyTokenBudget() == 0 && again.WeeklyTokenBudget() == 0);
    CHECK(again.SetSetting("MaxConcurrent", "3", error) && again.SetSetting("MaxHops", "99", error));
    CHECK(again.SetSetting("DefaultModel", "haiku", error) && again.SetSetting("DailyTokens", "50000", error));
    CHECK(again.SetSetting("WeeklyTokens", "300000", error));
    Vault third;
    CHECK(Vault::Open(dir, third, error));
    CHECK(third.MaxConcurrent() == 3 && third.MaxHops() == 20 && third.DefaultModel() == "haiku");
    CHECK(third.DailyTokenBudget() == 50000 && third.WeeklyTokenBudget() == 300000);
    CHECK(third.Language() == "en" && third.Name() == "Eski");
    CHECK(third.SetSetting("MaxConcurrent", "2", error));
    CHECK(third.MaxConcurrent() == 2);
    fsx::RemoveDirRecursive(dir);
}

void TestProjects() {
    const std::string dir = FreshDir("burrow_selftest_projects");
    std::string error;
    CHECK(Vault::Create("Projeler", dir, error));
    Vault vault;
    CHECK(Vault::Open(dir, vault, error));

    ProjectDefinition p;
    p.name = "Site Yenileme";
    p.summary = "Şirket sitesini yeniliyoruz.";
    CHECK(vault.SaveProject(p, error) && p.id == "site_yenileme");
    ProjectNote n1;
    n1.name = "Renkler";
    n1.body = "Ana renk lacivert.";
    ProjectNote n2;
    n2.name = "Eski karar";
    n2.body = "Eski bilgi kapalı.";
    n2.enabled = false;
    CHECK(vault.SaveNote(p.id, n1, error) && vault.SaveNote(p.id, n2, error));

    const std::vector<ProjectDefinition> loaded = vault.LoadProjects();
    CHECK(loaded.size() == 1 && loaded[0].name == p.name && loaded[0].notes.size() == 2);
    CHECK(loaded[0].EstimatedTokens() > 0);

    // Only the enabled notes reach the prompt, and only for jobs of that project.
    TeamContext team;
    team.projects = loaded;
    AgentDefinition ali;
    ali.id = "ali";
    ali.name = "Ali";
    team.agents = {ali};
    const RunJob in_project = MakeChatJob(team, ali, "c1", "", "x", "site_yenileme");
    CHECK(in_project.project_id == "site_yenileme");
    CHECK(Has(in_project.system_prompt, "lacivert") && Has(in_project.system_prompt, "yeniliyoruz"));
    CHECK(!Has(in_project.system_prompt, "Eski bilgi"));
    const RunJob outside = MakeChatJob(team, ali, "c1", "", "x");
    CHECK(outside.project_id.empty() && !Has(outside.system_prompt, "lacivert"));
    CHECK(MakeChatJob(team, ali, "c1", "", "x", "yok").project_id.empty());  // unknown project is ignored
    CHECK(Has(MakeTaskJob(team, ali, nullptr, JobSource::Scheduled, "site_yenileme").system_prompt, "lacivert"));
    CHECK(Has(MakeAgentMessageJob(team, ali, ali, "x", {{"user", "c1"}}, 1, true, "site_yenileme").system_prompt,
              "lacivert"));

    // A wake-up keeps its project, and old two-part wake-ups still parse.
    Wakeup w;
    CHECK(Wakeup::Parse("09:00 | gunluk | site_yenileme", w) && w.task_id == "gunluk" && w.project_id == "site_yenileme");
    Wakeup again;
    CHECK(Wakeup::Parse(w.Serialize(), again) && again.project_id == "site_yenileme");
    CHECK(Wakeup::Parse("every 30 | ", w) && w.project_id.empty());
    CHECK(Wakeup::Parse("09:00 | gunluk", w) && w.project_id.empty() && w.task_id == "gunluk");

    // Conversations remember their project.
    ConversationStore store(vault.SubDir("Conversations"));
    const std::string conv = store.Create("chat", "t", {"ali"}, "site_yenileme");
    ConversationInfo info;
    std::vector<ConvMessage> messages;
    CHECK(store.Load(conv, info, messages) && info.project == "site_yenileme");
    CHECK(store.Load(store.Create("chat", "t", {"ali"}), info, messages) && info.project.empty());

    std::string delete_error;
    CHECK(vault.DeleteProject(loaded[0], delete_error) && vault.LoadProjects().empty());
    fsx::RemoveDirRecursive(dir);
}

void TestUsageStats() {
    std::vector<RunRecord> records;
    auto add = [&](const char* time, const char* agent, const std::string& status, long tokens) {
        RunRecord r;
        r.time = time;
        r.agent_id = agent;
        r.status = status;
        r.tokens = tokens;
        records.push_back(r);
    };
    const std::string hata = RunStatusLabel(RunStatus::Failed);
    add("2026-10-29 09:00:00", "ali", "tamam", 100);
    add("2026-10-29 10:00:00", "ali", hata, 50);
    add("2026-10-25 10:00:00", "ali", "tamam", 1000);  // inside the 7 day window, not today
    add("2026-10-22 10:00:00", "ali", "tamam", 5000);  // 7 days ago: outside
    add("2026-10-29 11:00:00", "", "tamam", 7);         // usage probe, ignored
    const std::map<std::string, AgentUsage> usage = ComputeUsage(records, "2026-10-29");
    CHECK(usage.size() == 1);
    const AgentUsage& u = usage.at("ali");
    CHECK(u.tokens_today == 150 && u.runs_today == 2 && u.tokens_7d == 1150 && u.runs_7d == 3);
    CHECK(u.failures_7d == 1 && u.last_run == "2026-10-29 10:00:00");
    CHECK(WeekStart("2026-10-29") == "2026-10-23");
    CHECK(!OverDailyLimit(150, 0) && !OverDailyLimit(149, 150) && OverDailyLimit(150, 150));

    AgentDefinition a;
    a.id = "ali";
    a.name = "Ali";
    a.daily_token_limit = 5000;
    AgentDefinition b;
    CHECK(AgentDefinition::Parse("ali", a.Serialize(), b) && b.daily_token_limit == 5000);
    a.daily_token_limit = 0;
    CHECK(!Has(a.Serialize(), "daily_tokens"));

    const std::string dir = FreshDir("burrow_selftest_rename");
    ConversationStore store(dir);
    const std::string id = store.Create("chat", "Eski", {"ali"});
    store.Rename(id, "Yeni başlık");
    ConversationInfo info;
    std::vector<ConvMessage> messages;
    CHECK(store.Load(id, info, messages) && info.title == "Yeni başlık");
    fsx::RemoveDirRecursive(dir);
}

void TestReplyParsing() {
    ParsedReply r = ParseReply("Tamam.\n<send to=\"bob\">Merhaba\nBob</send>\nSon söz.");
    CHECK(r.sends.size() == 1 && r.sends[0].to == "bob" && r.sends[0].text == "Merhaba\nBob");
    CHECK(Has(r.text, "Tamam.") && Has(r.text, "Son söz.") && !Has(r.text, "<send"));
    r = ParseReply("Bilgi.<send to=\"x\" reply=\"no\">FYI</send>");
    CHECK(r.sends.size() == 1 && !r.sends[0].expects_reply && r.text == "Bilgi.");
    CHECK(ParseReply("<send to=\"x\">q</send>").sends[0].expects_reply);
    // The old stop marker is plain text now: it must not swallow anything.
    r = ParseReply("Cevap: 4\n\n[[SON]]");
    CHECK(Has(r.text, "Cevap: 4"));
    r = ParseReply("<send to=\"a\">1</send><send to=\"b\">2</send>");
    CHECK(r.sends.size() == 2 && r.sends[1].to == "b" && r.text.empty());
    r = ParseReply("<title>Hesap özeti</title>Merhaba!");
    CHECK(r.title == "Hesap özeti" && r.text == "Merhaba!");  // the title is cut out of the answer
    CHECK(ParseReply("Merhaba").title.empty());
    r = ParseReply("<send to=\"bob\" title=\"Rapor işi\">Hazırla</send>");
    CHECK(r.sends.size() == 1 && r.sends[0].title == "Rapor işi" && r.sends[0].expects_reply);
    r = ParseReply("<send to=\"a\">yarım");
    CHECK(r.sends.empty() && Has(r.text, "<send"));  // broken block is left alone

    // Telegram blocks are cut out of the answer and collected; an unfinished one stays as it is.
    r = ParseReply("Bitti.<telegram>Rapor hazır</telegram> Sonra <telegram>İkinci</telegram>");
    CHECK(r.telegram.size() == 2 && r.telegram[0] == "Rapor hazır" && r.telegram[1] == "İkinci");
    CHECK(Has(r.text, "Bitti.") && !Has(r.text, "telegram"));
    r = ParseReply("<telegram>yarım");
    CHECK(r.telegram.empty() && Has(r.text, "<telegram>"));
}

void TestTelegramConfig() {
    const std::string dir = FreshDir("burrow_selftest_telegram");
    fsx::MakeDirs(dir);
    const std::string file = fsx::Join(dir, "telegram.ini");
    CHECK(!TelegramConfig::Load(file).Ready());  // no file: not set up
    TelegramConfig c;
    c.token = "123:abc";
    CHECK(!c.Ready());
    c.chat_id = "-100200";
    c.notify_failures = true;
    c.receive = true;
    c.agent_id = "ayse";
    CHECK(c.Ready() && c.Save(file));
    const TelegramConfig back = TelegramConfig::Load(file);
    CHECK(back.token == "123:abc" && back.chat_id == "-100200" && back.notify_failures);
    CHECK(back.receive && back.agent_id == "ayse" && !TelegramConfig().receive);
    fsx::RemoveDirRecursive(dir);
}

void TestRemoteConfig() {
    const std::string dir = FreshDir("burrow_selftest_remote");
    fsx::MakeDirs(dir);
    const std::string file = fsx::Join(dir, "remote.ini");
    CHECK(RemoteConfig::Load(file).invites.empty());
    RemoteConfig c;
    c.invites.push_back({"a1", "viewer", "0123456789abcdef0123", "Ali | Veli", 0});
    c.invites.push_back({"b2", "editor", "fedcba9876543210fedc", "Ayşe", 0});
    CHECK(c.Save(file));
    const RemoteConfig back = RemoteConfig::Load(file);
    CHECK(back.invites.size() == 2 && back.Find("b2") && back.Find("b2")->role == "editor");
    CHECK(back.Find("a1")->name == "Ali   Veli" && back.Find("zz") == nullptr);  // '|' cannot break the line format
    // Roles: the viewer only reads, the editor does everything except the owner's own setup.
    CHECK(RemoteRoleAllows("viewer", "chats.list") && RemoteRoleAllows("viewer", "bootstrap"));
    CHECK(!RemoteRoleAllows("viewer", "chat.send") && !RemoteRoleAllows("viewer", "agent.save"));
    CHECK(RemoteRoleAllows("editor", "chat.send") && RemoteRoleAllows("editor", "skill.save"));
    CHECK(!RemoteRoleAllows("editor", "settings.set") && !RemoteRoleAllows("editor", "vault.open"));
    CHECK(!RemoteRoleAllows("editor", "remote.invite") && !RemoteRoleAllows("editor", "telegram.set"));
    CHECK(!RemoteRoleAllows("editor", "scheduler.set") && !RemoteRoleAllows("stranger", "chats.list"));
    fsx::RemoveDirRecursive(dir);
}

void TestWeekdayWakeups() {
    Wakeup w;
    CHECK(Wakeup::Parse("Mon,wed 09:00 | t | p", w) && w.days == 5 && w.time == "09:00" && w.task_id == "t" &&
          w.project_id == "p" && w.Serialize() == "mon,wed 09:00 | t | p" && w.Describe() == "Pzt,Çar 09:00");
    CHECK(!Wakeup::Parse("funday 09:00 | t", w) && !Wakeup::Parse("mon 25:00 | t", w));
    CHECK(Wakeup::Parse("mon,tue,wed,thu,fri,sat,sun 07:30 | t", w) && w.days == 0);

    AgentDefinition a;
    a.id = "a";
    Wakeup::Parse("sat 09:00 | t", w);  // 2026-11-07 is a Saturday, 11-05 a Thursday
    a.wakeups = {w};
    Scheduler s;
    s.Reset(timeutil::Make(2026, 11, 5, 8, 0, 0));
    CHECK(s.Collect({a}, timeutil::Make(2026, 11, 5, 9, 3, 0)).empty());
    CHECK(s.Collect({a}, timeutil::Make(2026, 11, 7, 9, 3, 0)).size() == 1);
}

void TestOneTimeWakeup() {
    Wakeup w;
    CHECK(Wakeup::Parse("2026-11-05 14:30 | t", w) && w.date == "2026-11-05" && w.time == "14:30" && w.days == 0 &&
          w.Serialize() == "2026-11-05 14:30 | t" && w.Describe() == "2026-11-05 14:30");
    CHECK(!Wakeup::Parse("2026-13-05 14:30 | t", w));
    Wakeup::Parse("2026-11-05 14:30 | t", w);
    AgentDefinition a;
    a.id = "a";
    a.wakeups = {w};
    Scheduler s;
    s.Reset(timeutil::Make(2026, 11, 5, 8, 0, 0));
    CHECK(s.Collect({a}, timeutil::Make(2026, 11, 5, 14, 29, 0)).empty());
    CHECK(s.Collect({a}, timeutil::Make(2026, 11, 5, 14, 31, 0)).size() == 1);
    CHECK(s.Collect({a}, timeutil::Make(2026, 11, 5, 14, 32, 0)).size() == 1);  // stays due until the owner removes it
    CHECK(s.Collect({a}, timeutil::Make(2026, 11, 5, 14, 50, 0)).empty());      // missed: not replayed late
    CHECK(s.Collect({a}, timeutil::Make(2026, 11, 6, 14, 31, 0)).empty());
}

void TestScheduler() {
    AgentDefinition a;
    a.id = "a";
    a.wakeups = {{"09:00", 0, "t", ""}, {"10:00", 0, "t", ""}, {"", 30, "", ""}};
    Scheduler s;
    s.Reset(timeutil::Make(2026, 11, 5, 8, 0, 0));

    auto due = s.Collect({a}, timeutil::Make(2026, 11, 5, 9, 3, 0));
    CHECK(due.size() == 2 && due[0].wakeup.time == "09:00" && due[1].wakeup.interval_minutes == 30);
    due = s.Collect({a}, timeutil::Make(2026, 11, 5, 9, 4, 0));
    CHECK(due.empty());  // nothing fires twice
    due = s.Collect({a}, timeutil::Make(2026, 11, 5, 9, 40, 0));
    CHECK(due.size() == 1 && due[0].wakeup.interval_minutes == 30);

    Scheduler late;  // switched on after 09:00: this morning's wake-up is not replayed
    late.Reset(timeutil::Make(2026, 11, 5, 9, 5, 0));
    CHECK(late.Collect({a}, timeutil::Make(2026, 11, 5, 9, 6, 0)).empty());

    a.enabled = false;
    CHECK(s.Collect({a}, timeutil::Make(2026, 11, 5, 12, 0, 0)).empty());
}

void TestConversationsAndJobs() {
    const std::string dir = FreshDir("burrow_selftest_conv");
    fsx::MakeDirs(dir);
    ConversationStore store(dir);
    const std::string id = store.Create("chat", "Deneme çş", {"ali"});
    store.Append(id, {std::string(), "user", "ali", "Merhaba\n\"dünya\""});
    store.SetSession(id, "sess-1");
    store.Append(id, {std::string(), "ali", "user", "Selam"});
    ConversationInfo info;
    std::vector<ConvMessage> msgs;
    CHECK(store.Load(id, info, msgs));
    CHECK(info.kind == "chat" && info.title == "Deneme çş" && info.session_id == "sess-1" &&
          info.participants.size() == 1);
    CHECK(msgs.size() == 2 && msgs[0].text == "Merhaba\n\"dünya\"" && msgs[1].from == "ali");
    store.AddParticipant(id, "veli");
    CHECK(store.List().size() == 1 && store.List()[0].participants.size() == 2);
    CHECK(store.Delete(id) && store.List().empty());
    fsx::RemoveDirRecursive(dir);

    TeamContext team;
    team.vault_root = "C:\\vault";
    team.skills = {{"wa", "WhatsApp", "", "WebFetch, mcp__wa__send(*)", "wa.json", "Mesajı wa ile gönder"}};
    AgentDefinition ali;
    ali.id = "ali"; ali.name = "Ali"; ali.role = "Asistan"; ali.skills = {"wa"}; ali.model = "sonnet";
    AgentDefinition veli;
    veli.id = "veli"; veli.name = "Veli"; veli.role = "Kontrol";
    team.agents = {ali, veli};
    TaskDefinition task;
    task.id = "t"; task.name = "Rapor"; task.body = "Rapor yaz"; task.model = "haiku"; task.priority = 2;
    task.min_remaining_percent = 50;
    team.tasks = {task};

    const RunJob job = MakeTaskJob(team, ali, &team.tasks[0], JobSource::Scheduled);
    CHECK(job.model == "haiku" && job.priority == 2 && job.min_remaining == 50 && job.prompt == "Rapor yaz");
    CHECK(job.tools == "WebFetch,mcp__wa__send(*)");
    CHECK(job.mcp_configs.size() == 1 && str::EndsWith(job.mcp_configs[0], "wa.json") &&
          !fsx::IsRelative(job.mcp_configs[0]));
    CHECK(Has(job.system_prompt, "Mesajı wa ile gönder") && Has(job.system_prompt, "veli: Veli"));
    CHECK(MakeChatJob(team, ali, "c1", "s1", "x").resume_session == "s1");
    CHECK(!MakeChatJob(team, ali, "c1", "", "x").name_chat && MakeChatJob(team, ali, "c1", "", "x").prompt == "x");
    const RunJob first = MakeChatJob(team, ali, "c1", "", "merhaba", "", true);
    CHECK(first.name_chat && Starts(first.prompt, "merhaba") && Has(first.prompt, "<title>"));
    const RunJob chat = MakeChatJob(team, ali, "c0", "", "x");
    CHECK(chat.reply_stack.size() == 1 && chat.reply_stack[0].agent_id == "user" && chat.ConversationId() == "c0");
    const std::vector<ReplyFrame> stack = {{"user", "c0"}, {"ali", "c2"}};
    const RunJob msg = MakeAgentMessageJob(team, veli, ali, "Selam", stack, 1, true);
    CHECK(msg.reply_stack.size() == 2 && msg.reply_stack.back().agent_id == "ali" && msg.hops == 1 &&
          Has(msg.prompt, "Selam") && Has(msg.system_prompt, "cevabın ona otomatik iletilir") &&
          msg.tools.empty() && msg.expects_reply);
    CHECK(!Has(MakeAgentMessageJob(team, veli, ali, "FYI", stack, 1, false).system_prompt, "otomatik iletilir"));
    const RunJob report = MakeReportJob(team, ali, veli, "Sonuç 42", {{"user", "c0"}}, "sess", 2);
    CHECK(report.agent_id == "ali" && report.resume_session == "sess" && report.persist_session &&
          Has(report.prompt, "Sonuç 42") && report.ConversationId() == "c0");

    // A second request from the same sender to the same agent is held back while the first is pending.
    {
        const std::string runs_dir = FreshDir("burrow_selftest_pending");
        fsx::MakeDirs(runs_dir);  // CancelAll writes run records, keep them out of the working folder
        RunManager pending;
        pending.SetVault(runs_dir, runs_dir);
        pending.max_concurrent = 0;  // nothing starts, the jobs stay queued
        CHECK(!pending.HasPendingRequest("veli", "ali"));
        pending.Enqueue(MakeAgentMessageJob(team, veli, ali, "Ölç", stack, 1, true));
        CHECK(pending.HasPendingRequest("veli", "ali") && !pending.HasPendingRequest("veli", "can") &&
              !pending.HasPendingRequest("ali", "veli"));
        pending.CancelAll();
        CHECK(!pending.HasPendingRequest("veli", "ali"));
        pending.Enqueue(MakeAgentMessageJob(team, veli, ali, "FYI", stack, 1, false));  // information only
        pending.Enqueue(MakeReportJob(team, veli, ali, "Sonuç", stack, "", 2));          // a report, not a request
        CHECK(!pending.HasPendingRequest("veli", "ali"));
        pending.CancelAll();
        fsx::RemoveDirRecursive(runs_dir);
    }

    // Company rules reach every agent's prompt; disabled ones do not.
    team.rules = {{"r1", "Hitap", true, "Kullanıcıya \"efendim\" diye hitap et."},
                  {"r2", "Kapalı", false, "BU KURAL KAPALI"}};
    const RunJob with_rules = MakeChatJob(team, veli, "c9", "", "x");
    CHECK(Has(with_rules.system_prompt, "efendim") && !Has(with_rules.system_prompt, "KAPALI"));
    CHECK(Has(MakeChatJob(team, ali, "c9", "", "x").system_prompt, "efendim"));
    CHECK(team.rules[0].EstimatedTokens() > 5 && team.rules[0].EstimatedTokens() < 40);
    RuleDefinition rule_back;
    CHECK(RuleDefinition::Parse("r1", team.rules[0].Serialize(), rule_back) && rule_back.enabled &&
          Has(rule_back.body, "efendim"));
}

// --- the process layer ----------------------------------------------------------------------

// Pumps Win32 messages and the run manager until done() or the timeout.
bool Pump(RunManager& runs, const std::function<bool()>& done, int timeout_ms) {
    const auto start = std::chrono::steady_clock::now();
    while (!done()) {
        runs.Tick();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() >
            timeout_ms)
            return false;
        Sleep(30);
    }
    return true;
}

void TestProcess() {
    // A real child process with stdin, stdout and an exit code: PowerShell reads the prompt from stdin.
    ClaudeProcess process;
    std::vector<std::string> lines;
    int exit_code = -1;
    bool exited = false;
    process.on_line = [&](const std::string& line) { lines.push_back(line); };
    process.on_exit = [&](int code) { exit_code = code; exited = true; };
    const std::string dir = fsx::TempDir();
    const bool started = process.Start(
        "powershell -NoProfile -Command \"$t = [Console]::In.ReadToEnd(); Write-Output ('got:' + $t.Trim()); "
        "Write-Output second; exit 3\"",
        dir, "hello çş");
    CHECK(started);
    for (int i = 0; started && i < 200 && !exited; ++i) {
        process.Poll();
        Sleep(50);
    }
    CHECK(exited && exit_code == 3);
    CHECK(lines.size() == 2 && lines[1] == "second");
    CHECK(!lines.empty() && str::StartsWith(lines[0], "got:hello"));

    // Stop() ends a process that would run for a long time.
    ClaudeProcess slow;
    bool slow_exited = false;
    slow.on_exit = [&](int) { slow_exited = true; };
    CHECK(slow.Start("powershell -NoProfile -Command \"Start-Sleep -Seconds 30\"", dir, ""));
    Sleep(300);
    slow.Stop();
    for (int i = 0; i < 100 && !slow_exited; ++i) {
        slow.Poll();
        Sleep(50);
    }
    CHECK(slow_exited);

    CHECK(!ClaudeProcess().Start("definitely_not_a_program_xyz --flag", dir, ""));
}

// --- live checks: the real Claude Code ------------------------------------------------------

class LiveTest {
public:
    LiveTest() : store_(std::string()), router_(&store_, &runs_) {}

    bool Run(const std::string& vault_dir) {
        fsx::RemoveDirRecursive(vault_dir);
        std::string error;
        CHECK(Vault::Create("Selftest", vault_dir, error));
        Vault::Open(vault_dir, vault_, error);
        store_ = ConversationStore(vault_.SubDir("Conversations"));
        runs_.SetVault(vault_dir, vault_.SubDir("Runs"));
        runs_.on_finished = [this](const RunResult& r) {
            Say(str::Format("  run finished: %s %s source=%s tokens=%ld text='%s'", r.job.agent_name.c_str(),
                            RunStatusLabel(r.status).c_str(), JobSourceLabel(r.job.source).c_str(), r.tokens,
                            str::Utf8Left(r.text, 80).c_str()));
            results_.push_back(r);
            router_.HandleResult(team_, r);
        };

        AgentDefinition alice, bob;
        alice.id = "alice"; alice.name = "Alice"; alice.role = "Koordinator"; alice.model = "haiku";
        bob.id = "bob"; bob.name = "Bob"; bob.role = "Matematikçi"; bob.model = "haiku";
        team_.vault_root = vault_dir;
        team_.agents = {alice, bob};

        auto idle = [this] { return runs_.RunningCount() + runs_.QueuedCount() == 0; };
        const int kTimeoutMs = 240000;

        Say("live 1/3: basic run + usage gate");
        RunJob plain;
        plain.agent_id = "alice"; plain.agent_name = "Alice";
        plain.prompt = "Sadece 'merhaba' yaz."; plain.system_prompt = "Cok kisa cevap ver.";
        plain.model = "haiku"; plain.max_turns = 1;
        runs_.Enqueue(plain);
        RunJob gated = plain;
        gated.min_remaining = 101;  // can never be satisfied
        gated.agent_name = "Alice (kapili)";
        runs_.Enqueue(gated);
        if (!Pump(runs_, idle, kTimeoutMs))
            return Timeout();

        CHECK(results_.size() == 2);
        if (results_.size() == 2) {
            CHECK(results_[0].status == RunStatus::Done && !results_[0].text.empty() && results_[0].tokens > 0);
            CHECK(results_[0].tokens < 5000);  // the lean profile keeps one call small
            CHECK(results_[1].status == RunStatus::Skipped);
        }
        CHECK(runs_.Usage().Known());
        Say(str::Format("  usage: %.0f%% left (5h %.0f%%, 7d %.0f%%)", runs_.Usage().RemainingPercent(),
                        runs_.Usage().five_hour.RemainingPercent(), runs_.Usage().seven_day.RemainingPercent()));
        CHECK(RunRecord::LoadAll(vault_.SubDir("Runs")).size() == 2);

        Say("live 2/3: user -> Alice -> Bob, Bob writes [[SON]] anyway, Alice must still report to the user");
        chat_conv_ = store_.Create("chat", "Deneme", {"alice"});
        store_.Append(chat_conv_, {std::string(), "user", "alice", "Bob'a soru ilet"});
        RunJob chat = MakeChatJob(team_, team_.agents[0], chat_conv_, std::string(),
                                  "Bob'a şu soruyu ilet: 2+2 kaçtır? Bunu mutlaka <send to=\"bob\">soru</send> "
                                  "bloğuyla yap, kendin cevaplama. Bob cevap verince sonucu bana bildirirsin.");
        chat.persist_session = false;
        chat.max_turns = 2;
        // Bob is told to end every answer with the old stop marker, the situation that broke the chain.
        team_.agents[1].persona = "Her cevabının sonuna [[SON]] yaz.";
        runs_.Enqueue(chat);
        if (!Pump(runs_, idle, kTimeoutMs))
            return Timeout();

        const auto convs = store_.List();
        const ConversationInfo* agents_conv = nullptr;
        for (const auto& c : convs)
            if (c.kind == "agents")
                agents_conv = &c;
        CHECK(agents_conv != nullptr);
        if (agents_conv) {
            ConversationInfo info;
            std::vector<ConvMessage> msgs;
            store_.Load(agents_conv->id, info, msgs);
            Say(str::Format("  agent conversation '%s' has %zu messages", info.title.c_str(), msgs.size()));
            for (const auto& m : msgs)
                Say("    " + m.from + " -> " + m.to + ": " + str::Utf8Left(m.text, 100));
            CHECK(msgs.size() == 2);
            CHECK(msgs.size() >= 2 && msgs[0].from == "alice" && msgs[1].from == "bob");
        }
        // The user's chat must now hold Alice's report: user -> alice, then alice -> user.
        ConversationInfo chat_info;
        std::vector<ConvMessage> chat_msgs;
        CHECK(store_.Load(chat_conv_, chat_info, chat_msgs));
        Say(str::Format("  user chat has %zu messages", chat_msgs.size()));
        for (const auto& m : chat_msgs)
            Say("    " + m.from + " -> " + m.to + ": " + str::Utf8Left(m.text, 100));
        CHECK(chat_msgs.size() == 2 && chat_msgs.back().from == "alice" && chat_msgs.back().to == "user");
        CHECK(!chat_msgs.empty() && Has(chat_msgs.back().text, "4"));
        int agent_runs = 0;
        for (const auto& r : results_)
            agent_runs += r.job.source == JobSource::Agent;
        CHECK(agent_runs == 2);  // Bob answers, Alice reports; nothing bounces back to Bob
        Say(str::Format("  agent runs in the chain: %d (limit %d)", agent_runs, kMaxAgentHops));

        Say("live 3/3: cancel");
        RunJob slow;
        slow.agent_id = "alice"; slow.agent_name = "Alice";
        slow.prompt = "Çok uzun bir hikaye yaz, en az 2000 kelime."; slow.system_prompt = "Yaz.";
        slow.model = "haiku"; slow.max_turns = 1;
        runs_.Enqueue(slow);
        Pump(runs_, [] { return false; }, 1500);  // let it get going
        runs_.CancelAll();
        if (!Pump(runs_, idle, kTimeoutMs))
            return Timeout();
        CHECK(!results_.empty() && results_.back().status == RunStatus::Stopped);
        return true;
    }

    ~LiveTest() { runs_.on_changed = nullptr; runs_.on_finished = nullptr; }

private:
    bool Timeout() {
        ++g_failures;
        std::printf("FAIL live test timed out\n");
        return false;
    }

    Vault vault_;
    ConversationStore store_;
    RunManager runs_;
    ConversationRouter router_;
    TeamContext team_;
    std::vector<RunResult> results_;
    std::string chat_conv_;
};
}  // namespace

int main(int argc, char** argv) {
    bool live = false;
    for (int i = 1; i < argc; ++i)
        live = live || std::strcmp(argv[i], "--live") == 0;

    TestText();
    TestJson();
    TestUsage();
    TestMarkdownAndAgents();
    TestHierarchy();
    TestSkillTemplates();
    TestVaultLanguage();
    TestTelegramConfig();
    TestRemoteConfig();
    TestProjects();
    TestUsageStats();
    TestReplyParsing();
    TestScheduler();
    TestWeekdayWakeups();
    TestOneTimeWakeup();
    TestConversationsAndJobs();
    TestProcess();
    std::printf("offline: %d passed, %d failed\n", g_passes, g_failures);

    if (live) {
        const std::string dir = TempPath("burrow_selftest_vault");
        {
            LiveTest test;
            test.Run(dir);
        }
        fsx::RemoveDirRecursive(dir);
    }
    std::printf("total: %d passed, %d failed\n", g_passes, g_failures);
    return g_failures == 0 ? 0 : 1;
}
