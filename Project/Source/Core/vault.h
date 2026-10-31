#pragma once

#include "Core/agent_definition.h"
#include "Core/project_definition.h"
#include "Core/rule_definition.h"
#include "Core/skill_definition.h"
#include "Core/task_definition.h"

#include <string>
#include <vector>

// A vault is a plain folder:
//   vault.ini   - name, format version and interface language
//   Agents/     - one .md file per agent
//   Tasks/      - one .md file per task
//   Skills/     - one .md file per skill
//   Rules/      - one .md file per company rule (added to every agent's prompt)
//   Projects/   - one folder per project, data in hidden .Burrow/: project.md and Notes/ (persistent project memory)
//   Conversations/ - one .jsonl file per chat / agent conversation
//   Runs/       - one .md record per Claude Code run
class Vault {
public:
    static bool Create(const std::string& name, const std::string& root, std::string& error);
    static bool Open(const std::string& root, Vault& out, std::string& error);

    const std::string& Name() const { return name_; }
    const std::string& Root() const { return root_; }
    // Interface language kept in vault.ini ("tr" or "en"); the kasa_ayarlari skill can change it too.
    const std::string& Language() const { return language_; }
    bool SetLanguage(const std::string& code, std::string& error);
    static std::string NormalizeLanguage(const std::string& code);  // anything but "en" becomes "tr"
    // Settings kept in vault.ini next to the language (0 = unlimited / default where noted).
    int MaxConcurrent() const { return max_concurrent_; }      // jobs run at the same time, 1..4
    int MaxHops() const { return max_hops_; }                  // agent-to-agent steps per chain, 1..20
    const std::string& DefaultModel() const { return default_model_; }  // used when agent and task name none
    long DailyTokenBudget() const { return daily_tokens_; }    // all agents together, 0 = none
    long WeeklyTokenBudget() const { return weekly_tokens_; }  // last 7 days, 0 = none
    // Writes one "Key=value" line of vault.ini (replaces it or appends it) and reloads the settings.
    bool SetSetting(const std::string& key, const std::string& value, std::string& error);
    // Re-reads vault.ini (an agent may have edited the name or the language).
    void ReloadSettings();

    std::vector<AgentDefinition> LoadAgents() const;
    // A new agent (empty id) gets a unique id derived from its name.
    bool SaveAgent(AgentDefinition& agent, std::string& error) const;
    bool DeleteAgent(const AgentDefinition& agent, std::string& error) const;

    std::vector<TaskDefinition> LoadTasks() const;
    bool SaveTask(TaskDefinition& task, std::string& error) const;
    bool DeleteTask(const TaskDefinition& task, std::string& error) const;

    std::vector<SkillDefinition> LoadSkills() const;
    bool SaveSkill(SkillDefinition& skill, std::string& error) const;
    bool DeleteSkill(const SkillDefinition& skill, std::string& error) const;

    std::vector<RuleDefinition> LoadRules() const;
    bool SaveRule(RuleDefinition& rule, std::string& error) const;
    bool DeleteRule(const RuleDefinition& rule, std::string& error) const;

    // Projects/<id>/.Burrow/project.md plus .Burrow/Notes/*.md (old layout is migrated on load). LoadProjects fills in the notes.
    std::vector<ProjectDefinition> LoadProjects() const;
    bool SaveProject(ProjectDefinition& project, std::string& error) const;  // saves project.md only
    bool DeleteProject(const ProjectDefinition& project, std::string& error) const;  // with all its notes
    bool SaveNote(const std::string& project_id, ProjectNote& note, std::string& error) const;
    bool DeleteNote(const std::string& project_id, const ProjectNote& note, std::string& error) const;

    // Full path of one of the vault's sub folders ("Agents", "Runs", ...).
    std::string SubDir(const char* name) const;

private:
    std::string name_;
    std::string root_;
    std::string language_ = "tr";
    int max_concurrent_ = 1;
    int max_hops_ = 8;
    std::string default_model_;
    long daily_tokens_ = 0;
    long weekly_tokens_ = 0;
};
