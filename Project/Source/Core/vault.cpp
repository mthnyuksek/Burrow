#include "Core/vault.h"

#include "Core/id_util.h"
#include "Core/platform.h"
#include "Core/skill_templates.h"
#include "Core/text.h"

#include <algorithm>

namespace {
constexpr const char* kConfigFile = "vault.ini";
constexpr const char* kSubDirs[] = {"Agents", "Tasks", "Skills", "Rules", "Projects", "Conversations", "Runs"};

using fsx::Join;
using fsx::ReadFile;
using fsx::WriteFile;

// Reads every "*.md" in dir and parses it with parse(id, text, item).
template <typename Item, typename ParseFn>
std::vector<Item> LoadAll(const std::string& dir_path, ParseFn parse) {
    std::vector<Item> items;
    for (const std::string& file : fsx::ListFiles(dir_path, ".md")) {
        std::string content;
        Item item;
        if (ReadFile(Join(dir_path, file), content) && parse(fsx::Stem(file), content, item))
            items.push_back(item);
    }
    return items;
}

// Gives a new item an id that is not yet used as a file name in dir.
void AssignUniqueId(const std::string& dir, const std::string& name, std::string& id) {
    const std::string base = MakeId(name);
    id = base;
    for (int n = 2; fsx::FileExists(Join(dir, id + ".md")); ++n)
        id = base + "_" + std::to_string(n);
}
}  // namespace

bool Vault::Create(const std::string& name, const std::string& root, std::string& error) {
    if (str::Trim(name).empty() || root.empty()) {
        error = "Kasa adı ve klasör yolu boş olamaz.";
        return false;
    }
    if (fsx::FileExists(Join(root, kConfigFile))) {
        error = "Bu klasörde zaten bir kasa var.";
        return false;
    }
    bool ok = fsx::MakeDirs(root);
    for (const char* sub : kSubDirs)
        ok = ok && fsx::MakeDirs(Join(root, sub));
    if (!ok) {
        error = "Klasörler oluşturulamadı.";
        return false;
    }
    if (!WriteFile(Join(root, kConfigFile), "[Vault]\nName=" + name + "\nVersion=2\nLanguage=tr\n")) {
        error = "vault.ini yazılamadı.";
        return false;
    }
    // Every new vault starts with the setup skills, so a beginner can let an agent build the rest.
    for (const SkillTemplate& t : SkillTemplates()) {
        if (t.installed_by_default &&
            !WriteFile(Join(Join(root, "Skills"), t.skill.id + ".md"), t.skill.Serialize())) {
            error = "Hazır yetenekler yazılamadı.";
            return false;
        }
    }
    return true;
}

bool Vault::Open(const std::string& root, Vault& out, std::string& error) {
    std::string content;
    if (!ReadFile(Join(root, kConfigFile), content)) {
        error = "Bu klasör bir kasa değil (vault.ini bulunamadı).";
        return false;
    }
    Vault v;
    v.root_ = root;
    v.ReloadSettings();
    for (const char* sub : kSubDirs)  // vaults made by older versions lack newer folders
        fsx::MakeDirs(v.SubDir(sub));
    out = v;
    return true;
}

void Vault::ReloadSettings() {
    const std::string last = fsx::LastDirName(root_);
    name_ = last.empty() ? root_ : last;
    language_ = "tr";
    std::string content;
    if (!ReadFile(Join(root_, kConfigFile), content))
        return;
    for (const std::string& raw : str::Split(content, '\n')) {
        const std::string line = str::Trim(raw);
        if (str::StartsWith(line, "Name="))
            name_ = line.substr(5);
        else if (str::StartsWith(line, "Language="))
            language_ = NormalizeLanguage(line.substr(9));
    }
}

std::string Vault::NormalizeLanguage(const std::string& code) {
    return str::Lower(str::Trim(code)) == "en" ? "en" : "tr";
}

bool Vault::SetLanguage(const std::string& code, std::string& error) {
    const std::string lang = NormalizeLanguage(code);
    std::string content;
    ReadFile(Join(root_, kConfigFile), content);
    std::vector<std::string> lines = str::Split(content, '\n');
    bool found = false;
    for (std::string& line : lines) {
        if (str::StartsWith(str::Trim(line), "Language=")) {
            line = "Language=" + lang;
            found = true;
        }
    }
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i)
        if (!lines[i].empty() || i + 1 != lines.size())  // drop only the empty tail after the last newline
            out += lines[i] + "\n";
    if (!found)
        out += "Language=" + lang + "\n";
    if (!WriteFile(Join(root_, kConfigFile), out)) {
        error = "vault.ini yazılamadı.";
        return false;
    }
    language_ = lang;
    return true;
}

std::string Vault::SubDir(const char* name) const { return Join(root_, name); }

std::vector<AgentDefinition> Vault::LoadAgents() const {
    return LoadAll<AgentDefinition>(SubDir("Agents"), AgentDefinition::Parse);
}

bool Vault::SaveAgent(AgentDefinition& agent, std::string& error) const {
    if (agent.id.empty())
        AssignUniqueId(SubDir("Agents"), agent.name, agent.id);
    if (!WriteFile(Join(SubDir("Agents"), agent.id + ".md"), agent.Serialize())) {
        error = "Ajan dosyası yazılamadı.";
        return false;
    }
    return true;
}

bool Vault::DeleteAgent(const AgentDefinition& agent, std::string& error) const {
    if (!fsx::RemoveFile(Join(SubDir("Agents"), agent.id + ".md"))) {
        error = "Ajan dosyası silinemedi.";
        return false;
    }
    return true;
}

std::vector<TaskDefinition> Vault::LoadTasks() const {
    return LoadAll<TaskDefinition>(SubDir("Tasks"), TaskDefinition::Parse);
}

bool Vault::SaveTask(TaskDefinition& task, std::string& error) const {
    if (task.id.empty())
        AssignUniqueId(SubDir("Tasks"), task.name, task.id);
    if (!WriteFile(Join(SubDir("Tasks"), task.id + ".md"), task.Serialize())) {
        error = "Görev dosyası yazılamadı.";
        return false;
    }
    return true;
}

bool Vault::DeleteTask(const TaskDefinition& task, std::string& error) const {
    if (!fsx::RemoveFile(Join(SubDir("Tasks"), task.id + ".md"))) {
        error = "Görev dosyası silinemedi.";
        return false;
    }
    return true;
}

std::vector<SkillDefinition> Vault::LoadSkills() const {
    return LoadAll<SkillDefinition>(SubDir("Skills"), SkillDefinition::Parse);
}

bool Vault::SaveSkill(SkillDefinition& skill, std::string& error) const {
    if (skill.id.empty())
        AssignUniqueId(SubDir("Skills"), skill.name, skill.id);
    if (!WriteFile(Join(SubDir("Skills"), skill.id + ".md"), skill.Serialize())) {
        error = "Beceri dosyası yazılamadı.";
        return false;
    }
    return true;
}

std::vector<RuleDefinition> Vault::LoadRules() const {
    return LoadAll<RuleDefinition>(SubDir("Rules"), RuleDefinition::Parse);
}

bool Vault::SaveRule(RuleDefinition& rule, std::string& error) const {
    if (rule.id.empty())
        AssignUniqueId(SubDir("Rules"), rule.name, rule.id);
    if (!WriteFile(Join(SubDir("Rules"), rule.id + ".md"), rule.Serialize())) {
        error = "Kural dosyası yazılamadı.";
        return false;
    }
    return true;
}

bool Vault::DeleteRule(const RuleDefinition& rule, std::string& error) const {
    if (!fsx::RemoveFile(Join(SubDir("Rules"), rule.id + ".md"))) {
        error = "Kural dosyası silinemedi.";
        return false;
    }
    return true;
}

std::vector<ProjectDefinition> Vault::LoadProjects() const {
    std::vector<ProjectDefinition> projects;
    const std::string root = SubDir("Projects");
    for (const std::string& folder : fsx::ListDirs(root)) {
        std::string content;
        ProjectDefinition project;
        if (!ReadFile(Join(Join(root, folder), "project.md"), content) ||
            !ProjectDefinition::Parse(folder, content, project))
            continue;
        project.notes = LoadAll<ProjectNote>(Join(Join(root, folder), "Notes"), ProjectNote::Parse);
        std::sort(project.notes.begin(), project.notes.end(), [](const ProjectNote& a, const ProjectNote& b) {
            return str::CmpNoCase(a.name, b.name) < 0;
        });
        projects.push_back(project);
    }
    return projects;
}

bool Vault::SaveProject(ProjectDefinition& project, std::string& error) const {
    const std::string root = SubDir("Projects");
    if (project.id.empty()) {
        const std::string base = MakeId(project.name);
        project.id = base;
        for (int n = 2; fsx::DirExists(Join(root, project.id)); ++n)
            project.id = base + "_" + std::to_string(n);
    }
    const std::string folder = Join(root, project.id);
    if (!fsx::MakeDirs(Join(folder, "Notes")) || !WriteFile(Join(folder, "project.md"), project.Serialize())) {
        error = "Proje dosyası yazılamadı.";
        return false;
    }
    return true;
}

bool Vault::DeleteProject(const ProjectDefinition& project, std::string& error) const {
    if (project.id.empty() || !fsx::RemoveDirRecursive(Join(SubDir("Projects"), project.id))) {
        error = "Proje klasörü silinemedi.";
        return false;
    }
    return true;
}

bool Vault::SaveNote(const std::string& project_id, ProjectNote& note, std::string& error) const {
    const std::string dir = Join(Join(SubDir("Projects"), project_id), "Notes");
    fsx::MakeDirs(dir);
    if (note.id.empty())
        AssignUniqueId(dir, note.name, note.id);
    if (!WriteFile(Join(dir, note.id + ".md"), note.Serialize())) {
        error = "Not dosyası yazılamadı.";
        return false;
    }
    return true;
}

bool Vault::DeleteNote(const std::string& project_id, const ProjectNote& note, std::string& error) const {
    if (!fsx::RemoveFile(Join(Join(Join(SubDir("Projects"), project_id), "Notes"), note.id + ".md"))) {
        error = "Not dosyası silinemedi.";
        return false;
    }
    return true;
}

bool Vault::DeleteSkill(const SkillDefinition& skill, std::string& error) const {
    if (!fsx::RemoveFile(Join(SubDir("Skills"), skill.id + ".md"))) {
        error = "Beceri dosyası silinemedi.";
        return false;
    }
    return true;
}
