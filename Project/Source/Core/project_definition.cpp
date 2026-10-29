#include "Core/project_definition.h"

#include "Core/markdown_file.h"
#include "Core/text.h"

namespace {
int TokensOf(const std::string& body) {
    const std::string trimmed = str::Trim(body);
    return trimmed.empty() ? 0 : static_cast<int>(trimmed.size() / 3) + 1;
}
}  // namespace

int ProjectDefinition::EstimatedTokens() const {
    int total = TokensOf(summary);
    for (const ProjectNote& n : notes)
        if (n.enabled)
            total += n.EstimatedTokens();
    return total;
}

std::string ProjectDefinition::Serialize() const {
    MarkdownDoc doc;
    doc.Add("name", name);
    doc.body = summary;
    return doc.Serialize();
}

bool ProjectDefinition::Parse(const std::string& id, const std::string& file_text, ProjectDefinition& out) {
    MarkdownDoc doc;
    if (!MarkdownDoc::Parse(file_text, doc))
        return false;
    ProjectDefinition def;
    def.id = id;
    def.name = doc.Get("name", id);
    def.summary = doc.body;
    out = def;
    return true;
}
