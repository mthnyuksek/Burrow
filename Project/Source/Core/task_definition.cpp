#include "Core/task_definition.h"

#include "Core/markdown_file.h"

#include <algorithm>

std::string TaskDefinition::Serialize() const {
    MarkdownDoc doc;
    doc.Add("name", name);
    doc.Add("model", model);
    doc.Add("priority", std::to_string(priority));
    doc.Add("min_remaining_percent", std::to_string(min_remaining_percent));
    doc.body = body;
    return doc.Serialize();
}

bool TaskDefinition::Parse(const std::string& id, const std::string& text, TaskDefinition& out) {
    MarkdownDoc doc;
    if (!MarkdownDoc::Parse(text, doc))
        return false;
    TaskDefinition def;
    def.id = id;
    def.name = doc.Get("name", id);
    def.model = doc.Get("model");
    def.priority = std::clamp(doc.GetInt("priority", kPriorityNormal), 0, 2);
    def.min_remaining_percent = std::clamp(doc.GetInt("min_remaining_percent"), 0, 100);
    def.body = doc.body;
    out = def;
    return true;
}
