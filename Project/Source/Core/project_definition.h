#pragma once

#include "Core/rule_definition.h"

#include <string>
#include <vector>

// A project note is the project's persistent memory: a short piece of text that every agent
// working or chatting in the project reads on every run. It has the same shape as a company
// rule (name, enabled, body, token estimate), so it reuses that type.
using ProjectNote = RuleDefinition;

// A project is a folder <vault>/Projects/<id>/ with .Burrow/project.md (name + summary) and
// .Burrow/Notes/<note_id>.md. Chats, wake-ups and agent messages can belong to a project; the job
// builder then adds the summary and the enabled notes to the agent's system prompt.
struct ProjectDefinition {
    std::string id;  // folder name
    std::string name;
    std::string summary;  // what the project is about; always part of the prompt
    std::vector<ProjectNote> notes;  // filled by Vault::LoadProjects

    // Rough tokens added to every run in this project: summary plus enabled notes.
    int EstimatedTokens() const;

    std::string Serialize() const;  // project.md only
    static bool Parse(const std::string& id, const std::string& text, ProjectDefinition& out);
};
