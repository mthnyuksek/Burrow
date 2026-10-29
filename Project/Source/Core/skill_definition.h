#pragma once

#include <string>

// A skill is a capability an agent can be given: instructions that are added to
// its system prompt plus the tools it may use. A skill like "send WhatsApp
// message" needs a tool that can really do it, usually an MCP server named in
// mcp_config.
struct SkillDefinition {
    std::string id;  // file name without extension
    std::string name;
    std::string description;
    std::string tools;       // comma separated tool names / patterns, e.g. "WebFetch, mcp__whatsapp__send_message"
    std::string mcp_config;  // optional: path to an MCP config json (relative to the vault or absolute)
    std::string body;        // how and when to use the skill

    std::string Serialize() const;
    static bool Parse(const std::string& id, const std::string& text, SkillDefinition& out);
};
