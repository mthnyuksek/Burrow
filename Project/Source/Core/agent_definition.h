#pragma once

#include <string>
#include <vector>

// One scheduled wake-up: either a daily time ("09:00") or a repeat interval,
// plus the task to do. An empty task_id means "follow the agent's own instructions".
struct Wakeup {
    std::string time;             // "HH:MM"; empty when interval_minutes is used
    int interval_minutes = 0;  // > 0 = wake every N minutes
    std::string task_id;
    std::string project_id;  // optional: the run happens inside this project (its notes are read)

    std::string Describe() const;  // "09:00" or "her 30 dk"
    std::string Serialize() const;
    static bool Parse(const std::string& text, Wakeup& out);
    static bool IsValidTime(const std::string& hhmm);
};

// One agent = one markdown file in <vault>/Agents. The header holds identity,
// look, boss and wake-ups; the text below it is the agent's persona / standing
// instructions.
struct AgentDefinition {
    std::string id;  // file name without extension
    std::string name;
    std::string role;  // short job title, e.g. "Araştırmacı"
    int mascot_species = 0;
    int mascot_color = 0;
    int mascot_accessory = 0;
    std::string reports_to;  // id of the boss agent, empty = nobody
    std::string model;       // used for chats and wake-ups without a task; empty = Claude Code default
    bool enabled = true;
    int daily_token_limit = 0;  // 0 = unlimited; scheduled wake-ups are skipped once today's tokens reach it
    std::vector<std::string> skills;  // ids of SkillDefinition
    std::vector<Wakeup> wakeups;
    std::string persona;

    std::string Serialize() const;
    static bool Parse(const std::string& id, const std::string& text, AgentDefinition& out);
};

// True if making boss_id the boss of agent_id would create a reporting loop.
bool WouldCreateCycle(const std::vector<AgentDefinition>& all, const std::string& agent_id,
                      const std::string& boss_id);

// Bosses first, each followed by their reports; siblings sorted by name.
std::vector<AgentDefinition> OrderByHierarchy(std::vector<AgentDefinition> agents);
