#include "Core/rule_definition.h"

#include "Core/markdown_file.h"
#include "Core/text.h"

int RuleDefinition::EstimatedTokens() const {
    const std::string trimmed = str::Trim(body);
    return trimmed.empty() ? 0 : static_cast<int>(trimmed.size() / 3) + 1;
}

std::string RuleDefinition::Serialize() const {
    MarkdownDoc doc;
    doc.Add("name", name);
    doc.Add("enabled", enabled ? "true" : "false");
    doc.body = body;
    return doc.Serialize();
}

bool RuleDefinition::Parse(const std::string& id, const std::string& file_text, RuleDefinition& out) {
    MarkdownDoc doc;
    if (!MarkdownDoc::Parse(file_text, doc))
        return false;
    RuleDefinition def;
    def.id = id;
    def.name = doc.Get("name", id);
    def.enabled = str::CmpNoCase(doc.Get("enabled", "true"), "true") == 0;
    def.body = doc.body;
    out = def;
    return true;
}
