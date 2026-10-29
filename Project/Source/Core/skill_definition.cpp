#include "Core/skill_definition.h"

#include "Core/markdown_file.h"

std::string SkillDefinition::Serialize() const {
    MarkdownDoc doc;
    doc.Add("name", name);
    doc.Add("description", description);
    doc.Add("tools", tools);
    doc.Add("mcp_config", mcp_config);
    doc.body = body;
    return doc.Serialize();
}

bool SkillDefinition::Parse(const std::string& id, const std::string& text, SkillDefinition& out) {
    MarkdownDoc doc;
    if (!MarkdownDoc::Parse(text, doc))
        return false;
    SkillDefinition def;
    def.id = id;
    def.name = doc.Get("name", id);
    def.description = doc.Get("description");
    def.tools = doc.Get("tools");
    def.mcp_config = doc.Get("mcp_config");
    def.body = doc.body;
    out = def;
    return true;
}
