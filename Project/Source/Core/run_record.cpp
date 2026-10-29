#include "Core/run_record.h"

#include "Core/markdown_file.h"
#include "Core/platform.h"
#include "Core/text.h"

#include <algorithm>

bool RunRecord::Save(const std::string& runs_dir) const {
    MarkdownDoc doc;
    doc.Add("time", time);
    doc.Add("agent_id", agent_id);
    doc.Add("agent_name", agent_name);
    doc.Add("task", task_name);
    doc.Add("source", source);
    doc.Add("status", status);
    doc.Add("model", model);
    doc.Add("tokens", str::Format("%ld", tokens));
    doc.Add("cost_usd", str::Format("%.5f", cost_usd));
    doc.Add("duration_s", str::Format("%.1f", duration_s));
    doc.Add("conversation", conversation_id);
    doc.body = text;
    return fsx::WriteFile(fsx::Join(runs_dir, id + ".md"), doc.Serialize());
}

std::vector<RunRecord> RunRecord::LoadAll(const std::string& runs_dir) {
    std::vector<RunRecord> records;
    for (const std::string& file : fsx::ListFiles(runs_dir, ".md")) {
        std::string content;
        MarkdownDoc doc;
        if (!fsx::ReadFile(fsx::Join(runs_dir, file), content) || !MarkdownDoc::Parse(content, doc))
            continue;
        RunRecord r;
        r.id = fsx::Stem(file);
        r.time = doc.Get("time");
        r.agent_id = doc.Get("agent_id");
        r.agent_name = doc.Get("agent_name");
        r.task_name = doc.Get("task");
        r.source = doc.Get("source");
        r.status = doc.Get("status");
        r.model = doc.Get("model");
        r.tokens = doc.GetInt("tokens");
        str::ToDouble(doc.Get("cost_usd"), r.cost_usd);
        str::ToDouble(doc.Get("duration_s"), r.duration_s);
        r.conversation_id = doc.Get("conversation");
        r.text = doc.body;
        records.push_back(r);
    }
    std::sort(records.begin(), records.end(),
              [](const RunRecord& a, const RunRecord& b) { return a.id > b.id; });
    return records;
}
