#pragma once

#include <string>

// A company rule: a prompt that is added to the system prompt of every agent, so the same
// instructions do not have to be written into each agent. Every enabled rule is paid for
// again on every single run, so EstimatedTokens() is shown next to it.
struct RuleDefinition {
    std::string id;  // file name without extension
    std::string name;
    bool enabled = true;
    std::string body;

    int EstimatedTokens() const;  // rough: Turkish text is about 3 bytes per token

    std::string Serialize() const;
    static bool Parse(const std::string& id, const std::string& text, RuleDefinition& out);
};
