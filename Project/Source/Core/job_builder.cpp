#include "Core/job_builder.h"

#include "Core/platform.h"
#include "Core/text.h"

#include <algorithm>

namespace {
constexpr size_t kMaxTeamListed = 15;

enum class Asked { No, ExpectingReply, InformationOnly };

// The system prompt is kept short on purpose: it is paid for on every single run.
std::string BuildSystemPrompt(const TeamContext& team, const AgentDefinition& agent,
                              const AgentDefinition* asked_by, Asked asked, const ProjectDefinition* project) {
    std::string p;
    p += "Sen \"" + agent.name + "\" adında bir ajansın.";
    if (!agent.role.empty())
        p += " Rolün: " + agent.role + ".";
    p += " Türkçe, kısa ve öz yaz.\n";
    // Short on purpose (~100 tokens per run): enough that an agent can answer "where am I?" without reading files.
    p += "Burrow uygulamasında çalışıyorsun: kullanıcı ajan ekibini bu uygulamayla yönetir, "
         "uygulama seni sohbetten ya da zamanlanmış uyanıştan başlatır. Çalışma klasörün kullanıcının kasası "
         "(düz dosyalar): Agents/ Tasks/ Skills/ Rules/ Projects/ (proje notları) Conversations/ Runs/ (kayıtlar). "
         "Dosya ve araç erişimin yalnızca yeteneklerinin verdiğiyle sınırlı; bu bilgi yetiyorsa dosya okumadan cevapla.\n";

    std::string rules;
    for (const RuleDefinition& rule : team.rules)
        if (rule.enabled && !str::Trim(rule.body).empty())
            rules += str::Trim(rule.body) + "\n\n";
    if (!rules.empty())
        p += "\n## Şirket kuralları\n" + str::Trim(rules) + "\n";

    if (!str::Trim(agent.persona).empty())
        p += "\n" + str::Trim(agent.persona) + "\n";

    if (project) {
        p += "\n## Proje: " + project->name + " (id: " + project->id + ")\n";
        if (!str::Trim(project->summary).empty())
            p += str::Trim(project->summary) + "\n";
        for (const ProjectNote& note : project->notes)
            if (note.enabled && !str::Trim(note.body).empty())
                p += "\n### " + note.name + "\n" + str::Trim(note.body) + "\n";
    }

    for (const std::string& skill_id : agent.skills)
        for (const SkillDefinition& s : team.skills)
            if (s.id == skill_id)
                p += "\n## " + s.name + "\n" + str::Trim(s.body) + "\n";

    size_t listed = 0;
    std::string crew;
    for (const AgentDefinition& other : team.agents) {
        if (other.id == agent.id || !other.enabled || listed >= kMaxTeamListed)
            continue;
        crew += "- " + other.id + ": " + other.name;
        if (!other.role.empty())
            crew += " (" + other.role + ")";
        crew += "\n";
        ++listed;
    }
    if (!crew.empty()) {
        p += "\n## Ekip\n" + crew;
        p += "Başka bir ajana iş ya da soru iletmek için cevabına şu bloğu ekle: "
             "<send to=\"ajan_id\">mesaj</send>\n"
             "Sonucu otomatik olarak sana gelir; sonra bunu isteyen kişiye sen özetlersin. "
             "Sadece bilgi veriyorsan ve cevap istemiyorsan "
             "<send to=\"ajan_id\" reply=\"no\"> kullan. Yeni bir konuşma başlatıyorsan etikete 2-4 kelimelik başlık ekle: "
             "<send to=\"ajan_id\" title=\"Kısa başlık\">\n";
    }
    if (asked_by && asked == Asked::ExpectingReply)
        p += "\n" + asked_by->name + " sana yazdı; cevabın ona otomatik iletilir.\n";
    else if (asked_by && asked == Asked::InformationOnly)
        p += "\n" + asked_by->name + " sana bilgi verdi; cevap beklemiyor, gerekmedikçe kısa tut.\n";
    return p;
}

// Collects the tools and MCP configs of the agent's skills.
void ApplySkills(const TeamContext& team, const AgentDefinition& agent, RunJob& job) {
    std::string tools;
    for (const std::string& skill_id : agent.skills)
        for (const SkillDefinition& s : team.skills) {
            if (s.id != skill_id)
                continue;
            for (const std::string& raw : str::Split(s.tools, ',')) {
                const std::string tool = str::Trim(raw);
                if (!tool.empty() && !str::Contains(tools, tool))
                    tools += (tools.empty() ? "" : ",") + tool;
            }
            if (!s.mcp_config.empty()) {
                const std::string full = fsx::MakeAbsolute(s.mcp_config, team.vault_root);
                if (std::find(job.mcp_configs.begin(), job.mcp_configs.end(), full) == job.mcp_configs.end())
                    job.mcp_configs.push_back(full);
            }
        }
    job.tools = tools;
}

RunJob BaseJob(const TeamContext& team, const AgentDefinition& agent, const AgentDefinition* asked_by,
               Asked asked, const std::string& project_id) {
    RunJob job;
    job.agent_id = agent.id;
    job.agent_name = agent.name;
    job.model = agent.model;
    const ProjectDefinition* project = team.FindProject(project_id);
    if (project)
        job.project_id = project->id;
    job.system_prompt = BuildSystemPrompt(team, agent, asked_by, asked, project);
    ApplySkills(team, agent, job);
    return job;
}
}  // namespace

const AgentDefinition* TeamContext::FindAgent(const std::string& id) const {
    for (const AgentDefinition& a : agents)
        if (a.id == id)
            return &a;
    return nullptr;
}

const ProjectDefinition* TeamContext::FindProject(const std::string& id) const {
    if (id.empty())
        return nullptr;
    for (const ProjectDefinition& p : projects)
        if (p.id == id)
            return &p;
    return nullptr;
}

const TaskDefinition* TeamContext::FindTask(const std::string& id) const {
    for (const TaskDefinition& t : tasks)
        if (t.id == id)
            return &t;
    return nullptr;
}

RunJob MakeChatJob(const TeamContext& team, const AgentDefinition& agent, const std::string& conversation_id,
                   const std::string& session_id, const std::string& user_text, const std::string& project_id,
                   bool name_chat) {
    RunJob job = BaseJob(team, agent, nullptr, Asked::No, project_id);
    job.source = JobSource::Chat;
    job.reply_stack = {{"user", conversation_id}};
    job.prompt = user_text;
    if (name_chat) {  // only the first message pays these few tokens
        job.name_chat = true;
        job.prompt += "\n\n[Sistem notu: Bu sohbetin henüz adı yok. Cevabının en başına <title>"
                      "2-4 kelimelik kısa ve net bir ad</title> etiketi koy, sonra normal cevap ver.]";
    }
    job.resume_session = session_id;
    job.persist_session = true;  // chats continue the same Claude session, which keeps them cheap
    job.max_turns = 12;
    job.priority = kPriorityHigh;  // somebody is waiting for the answer
    return job;
}

RunJob MakeTaskJob(const TeamContext& team, const AgentDefinition& agent, const TaskDefinition* task,
                   JobSource source, const std::string& project_id) {
    RunJob job = BaseJob(team, agent, nullptr, Asked::No, project_id);
    job.source = source;
    job.max_turns = 15;
    if (task) {
        job.task_name = task->name;
        job.prompt = task->body;
        if (!task->model.empty())
            job.model = task->model;
        job.priority = task->priority;
        job.min_remaining = task->min_remaining_percent;
    } else {
        job.prompt = "Uyandın. Kalıcı talimatlarına göre şimdi yapman gereken işi yap ve sonucu kısaca bildir.";
    }
    return job;
}

RunJob MakeAgentMessageJob(const TeamContext& team, const AgentDefinition& to, const AgentDefinition& from,
                           const std::string& body, const std::vector<ReplyFrame>& reply_stack, int hops,
                           bool expects_reply, const std::string& project_id) {
    RunJob job = BaseJob(team, to, &from, expects_reply ? Asked::ExpectingReply : Asked::InformationOnly,
                         project_id);
    job.source = JobSource::Agent;
    job.reply_stack = reply_stack;
    job.expects_reply = expects_reply;
    job.hops = hops;
    job.prompt = from.name + " yazdı:\n" + body;
    job.max_turns = 8;
    return job;
}

RunJob MakeReportJob(const TeamContext& team, const AgentDefinition& agent, const AgentDefinition& from,
                     const std::string& result, const std::vector<ReplyFrame>& reply_stack,
                     const std::string& session_id, int hops, const std::string& project_id) {
    RunJob job = BaseJob(team, agent, nullptr, Asked::No, project_id);
    job.source = JobSource::Agent;
    job.reply_stack = reply_stack;
    job.hops = hops;
    job.prompt = from.name + " istediğin işi bitirdi ve şunu bildirdi:\n" + result +
                 "\n\nBu sonucu değerlendir ve işi isteyen kişiye kısaca bildir.";
    job.resume_session = session_id;
    job.persist_session = !session_id.empty();
    job.max_turns = 8;
    return job;
}

ParsedReply ParseReply(const std::string& raw_text) {
    ParsedReply reply;
    std::string raw = raw_text;
    const size_t title_open = raw.find("<title>");
    const size_t title_close = raw.find("</title>");
    if (title_open != std::string::npos && title_close != std::string::npos && title_close > title_open) {
        reply.title = str::Trim(raw.substr(title_open + 7, title_close - title_open - 7));
        raw = raw.substr(0, title_open) + raw.substr(title_close + 8);
    }
    while (true) {  // <telegram> blocks are cut out first, they never reach the transcript
        const size_t open = raw.find("<telegram>");
        const size_t close = raw.find("</telegram>");
        if (open == std::string::npos || close == std::string::npos || close < open)
            break;
        const std::string text = str::Trim(raw.substr(open + 10, close - open - 10));
        if (!text.empty())
            reply.telegram.push_back(text);
        raw = raw.substr(0, open) + raw.substr(close + 11);
    }
    std::string rest = raw;
    std::string kept;
    while (true) {
        const size_t open = rest.find("<send to=\"");
        if (open == std::string::npos)
            break;
        const std::string after_open = rest.substr(open + 10);
        const size_t quote = after_open.find('"');
        const size_t tag_end = after_open.find('>');
        const size_t close = after_open.find("</send>");
        if (quote == std::string::npos || tag_end == std::string::npos || close == std::string::npos ||
            tag_end < quote || close < tag_end)
            break;
        kept += rest.substr(0, open);
        SendDirective send;
        send.to = after_open.substr(0, quote);
        send.text = str::Trim(after_open.substr(tag_end + 1, close - tag_end - 1));
        const std::string attributes = after_open.substr(quote + 1, tag_end - quote - 1);
        send.expects_reply = !str::Contains(attributes, "reply=\"no\"");
        const size_t title_at = attributes.find("title=\"");
        if (title_at != std::string::npos)
            send.title = str::Trim(str::BeforeFirst(attributes.substr(title_at + 7), '"'));
        reply.sends.push_back(send);
        rest = after_open.substr(close + 7);
    }
    kept += rest;
    reply.text = str::Trim(kept);
    return reply;
}
