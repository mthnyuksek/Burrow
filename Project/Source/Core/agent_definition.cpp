#include "Core/agent_definition.h"

#include "Core/markdown_file.h"
#include "Core/text.h"

#include <algorithm>
#include <set>

namespace {
const char* const kDayKeys[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
const char* const kDayNamesTr[7] = {"Pzt", "Sal", "Çar", "Per", "Cum", "Cmt", "Paz"};

std::string DayList(int days, const char* const* names) {
    std::string out;
    for (int i = 0; i < 7; ++i)
        if (days >> i & 1)
            out += (out.empty() ? "" : ",") + std::string(names[i]);
    return out;
}
}  // namespace

std::string Wakeup::Describe() const {
    if (interval_minutes > 0)
        return str::Format("her %d dk", interval_minutes);
    return days == 0 ? time : DayList(days, kDayNamesTr) + " " + time;
}

std::string Wakeup::Serialize() const {
    std::string when = interval_minutes > 0 ? str::Format("every %d", interval_minutes) : time;
    if (interval_minutes <= 0 && days != 0)
        when = DayList(days, kDayKeys) + " " + time;
    return when + " | " + task_id + (project_id.empty() ? std::string() : " | " + project_id);
}

bool Wakeup::Parse(const std::string& line, Wakeup& out) {
    // "<when> | <task_id> | <project_id>", the last two are optional
    const size_t bar = line.find('|');
    const std::string when = str::Trim(bar == std::string::npos ? line : line.substr(0, bar));
    Wakeup w;
    if (bar != std::string::npos) {
        const std::string rest = line.substr(bar + 1);
        w.task_id = str::Trim(str::BeforeFirst(rest, '|'));
        if (str::Contains(rest, "|"))
            w.project_id = str::Trim(str::AfterFirst(rest, '|'));
    }
    if (str::StartsWith(when, "every ")) {
        long n = 0;
        if (!str::ToLong(str::Trim(when.substr(6)), n) || n <= 0)
            return false;
        w.interval_minutes = static_cast<int>(n);
    } else {
        // "09:00" or "mon,wed 09:00"
        const size_t space = when.find(' ');
        std::string clock = when;
        if (space != std::string::npos) {
            clock = str::Trim(when.substr(space + 1));
            for (const std::string& d : str::Split(when.substr(0, space), ',')) {
                int bit = -1;
                for (int i = 0; i < 7; ++i)
                    if (str::CmpNoCase(str::Trim(d), kDayKeys[i]) == 0)
                        bit = i;
                if (bit < 0)
                    return false;
                w.days |= 1 << bit;
            }
        }
        if (!IsValidTime(clock))
            return false;
        w.time = clock;
        if (w.days == 127)
            w.days = 0;  // all seven = every day
    }
    out = w;
    return true;
}

bool Wakeup::IsValidTime(const std::string& hhmm) {
    long h = 0, m = 0;
    const size_t colon = hhmm.find(':');
    return colon != std::string::npos && str::ToLong(hhmm.substr(0, colon), h) &&
           str::ToLong(hhmm.substr(colon + 1), m) && h >= 0 && h <= 23 && m >= 0 && m <= 59;
}

std::string AgentDefinition::Serialize() const {
    MarkdownDoc doc;
    doc.Add("name", name);
    doc.Add("role", role);
    doc.Add("mascot_species", std::to_string(mascot_species));
    doc.Add("mascot_color", std::to_string(mascot_color));
    doc.Add("mascot_accessory", std::to_string(mascot_accessory));
    doc.Add("reports_to", reports_to);
    doc.Add("model", model);
    doc.Add("enabled", enabled ? "true" : "false");
    if (daily_token_limit > 0)
        doc.Add("daily_tokens", std::to_string(daily_token_limit));
    for (const std::string& s : skills)
        doc.Add("skill", s);
    for (const Wakeup& w : wakeups)
        doc.Add("wake", w.Serialize());
    doc.body = persona;
    return doc.Serialize();
}

bool AgentDefinition::Parse(const std::string& id, const std::string& file_text, AgentDefinition& out) {
    MarkdownDoc doc;
    if (!MarkdownDoc::Parse(file_text, doc))
        return false;

    AgentDefinition def;
    def.id = id;
    def.name = doc.Get("name", id);
    def.role = doc.Get("role");
    def.mascot_species = doc.GetInt("mascot_species");
    def.mascot_color = doc.GetInt("mascot_color");
    def.mascot_accessory = doc.GetInt("mascot_accessory");
    def.reports_to = doc.Get("reports_to");
    def.model = doc.Get("model");
    def.enabled = str::CmpNoCase(doc.Get("enabled", "true"), "true") == 0;
    def.daily_token_limit = std::max(0, doc.GetInt("daily_tokens"));
    def.skills = doc.GetAll("skill");
    def.persona = doc.body;

    for (const std::string& line : doc.GetAll("wake")) {
        Wakeup w;
        if (Wakeup::Parse(line, w))
            def.wakeups.push_back(w);
    }
    // First file format: "wake_times: 09:00, 18:00" and "interval_minutes: N".
    for (const std::string& part : str::Split(doc.Get("wake_times"), ',')) {
        Wakeup w;
        if (Wakeup::Parse(part, w))
            def.wakeups.push_back(w);
    }
    if (doc.GetInt("interval_minutes") > 0) {
        Wakeup w;
        w.interval_minutes = doc.GetInt("interval_minutes");
        def.wakeups.push_back(w);
    }
    out = def;
    return true;
}

bool WouldCreateCycle(const std::vector<AgentDefinition>& all, const std::string& agent_id,
                      const std::string& boss_id) {
    std::string current = boss_id;
    for (size_t steps = 0; steps <= all.size(); ++steps) {
        if (current.empty())
            return false;
        if (current == agent_id)
            return true;
        const auto it = std::find_if(all.begin(), all.end(),
                                     [&](const AgentDefinition& a) { return a.id == current; });
        if (it == all.end())
            return false;
        current = it->reports_to;
    }
    return true;  // the chain already loops
}

std::vector<AgentDefinition> OrderByHierarchy(std::vector<AgentDefinition> agents) {
    std::sort(agents.begin(), agents.end(), [](const AgentDefinition& a, const AgentDefinition& b) {
        return str::CmpNoCase(a.name, b.name) < 0;
    });
    std::set<std::string> ids;
    for (const auto& a : agents)
        ids.insert(a.id);

    std::vector<AgentDefinition> ordered;
    std::set<std::string> placed;
    // Depth-first from every agent whose boss is missing; the lambda places a subtree.
    auto place = [&](const AgentDefinition& root, auto&& self) -> void {
        if (!placed.insert(root.id).second)
            return;
        ordered.push_back(root);
        for (const auto& child : agents)
            if (child.reports_to == root.id)
                self(child, self);
    };
    for (const auto& a : agents)
        if (a.reports_to.empty() || !ids.count(a.reports_to))
            place(a, place);
    for (const auto& a : agents)  // members of a reporting loop, if any
        place(a, place);
    return ordered;
}
