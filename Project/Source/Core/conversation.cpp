#include "Core/conversation.h"

#include "Core/id_util.h"
#include "Core/json.h"
#include "Core/platform.h"
#include "Core/text.h"

#include <algorithm>

namespace {
bool ReadLines(const std::string& path, std::vector<std::string>& lines) {
    std::string all;
    if (!fsx::ReadFile(path, all))
        return false;
    size_t start = 0;
    while (start < all.size()) {
        size_t end = all.find('\n', start);
        if (end == std::string::npos)
            end = all.size();
        if (end > start)
            lines.push_back(all.substr(start, end - start));
        start = end + 1;
    }
    return true;
}

ConversationInfo ReadMeta(const std::string& id, const std::vector<std::string>& lines) {
    ConversationInfo info;
    info.id = id;
    for (const std::string& line : lines) {
        Json j;
        if (!Json::Parse(line, j))
            continue;
        const std::string type = j["type"].AsString();
        if (type == "meta") {
            info.kind = j["kind"].AsString();
            info.title = j["title"].AsString();
            info.updated = j["created"].AsString();
            info.project = j["project"].AsString();
            for (size_t i = 0; i < j["participants"].Size(); ++i)
                info.participants.push_back(j["participants"].At(i).AsString());
        } else if (type == "participant") {
            info.participants.push_back(j["id"].AsString());
        } else if (type == "title") {
            info.title = j["title"].AsString();
        } else if (type == "session") {
            info.session_id = j["id"].AsString();
        } else if (type == "msg") {
            info.updated = j["t"].AsString();
        }
    }
    return info;
}
}  // namespace

std::string ConversationStore::Now() { return timeutil::Format(timeutil::Now(), "%Y-%m-%d %H:%M:%S"); }

std::string ConversationStore::PathOf(const std::string& id) const { return fsx::Join(dir_, id + ".jsonl"); }

void ConversationStore::AppendLine(const std::string& id, const std::string& json_line) {
    fsx::AppendFile(PathOf(id), json_line + "\n");
}

std::vector<ConversationInfo> ConversationStore::List() const {
    std::vector<ConversationInfo> result;
    for (const std::string& file : fsx::ListFiles(dir_, ".jsonl")) {
        std::vector<std::string> lines;
        const std::string id = fsx::Stem(file);
        if (ReadLines(PathOf(id), lines))
            result.push_back(ReadMeta(id, lines));
    }
    std::sort(result.begin(), result.end(), [](const ConversationInfo& a, const ConversationInfo& b) {
        return a.updated > b.updated;
    });
    return result;
}

bool ConversationStore::Load(const std::string& id, ConversationInfo& info,
                             std::vector<ConvMessage>& messages) const {
    std::vector<std::string> lines;
    if (!ReadLines(PathOf(id), lines))
        return false;
    info = ReadMeta(id, lines);
    messages.clear();
    for (const std::string& line : lines) {
        Json j;
        if (!Json::Parse(line, j) || j["type"].AsString() != "msg")
            continue;
        messages.push_back({j["t"].AsString(), j["from"].AsString(), j["to"].AsString(), j["text"].AsString()});
    }
    return true;
}

std::string ConversationStore::Create(const std::string& kind, const std::string& title,
                                      const std::vector<std::string>& participants, const std::string& project) {
    fsx::MakeDirs(dir_);
    const std::string stamp = timeutil::Format(timeutil::Now(), "%Y%m%d_%H%M%S");
    const std::string base = kind + "_" + stamp;
    std::string id = base;
    for (int n = 2; fsx::FileExists(PathOf(id)); ++n)
        id = base + "_" + std::to_string(n);

    Json meta = Json::MakeObject();
    meta.Set("type", Json::MakeString("meta"));
    meta.Set("kind", Json::MakeString(kind));
    meta.Set("title", Json::MakeString(title));
    meta.Set("created", Json::MakeString(Now()));
    if (!project.empty())
        meta.Set("project", Json::MakeString(project));
    Json people = Json::MakeArray();
    for (const std::string& p : participants)
        people.Push(Json::MakeString(p));
    meta.Set("participants", people);
    AppendLine(id, meta.Dump());
    return id;
}

void ConversationStore::AddParticipant(const std::string& id, const std::string& agent_id) {
    ConversationInfo info;
    std::vector<ConvMessage> messages;
    if (!Load(id, info, messages))
        return;
    if (std::find(info.participants.begin(), info.participants.end(), agent_id) != info.participants.end())
        return;
    Json j = Json::MakeObject();
    j.Set("type", Json::MakeString("participant"));
    j.Set("id", Json::MakeString(agent_id));
    AppendLine(id, j.Dump());
}

void ConversationStore::Append(const std::string& id, const ConvMessage& m) {
    Json j = Json::MakeObject();
    j.Set("type", Json::MakeString("msg"));
    j.Set("t", Json::MakeString(m.time.empty() ? Now() : m.time));
    j.Set("from", Json::MakeString(m.from));
    j.Set("to", Json::MakeString(m.to));
    j.Set("text", Json::MakeString(m.text));
    AppendLine(id, j.Dump());
}

void ConversationStore::SetSession(const std::string& id, const std::string& session_id) {
    Json j = Json::MakeObject();
    j.Set("type", Json::MakeString("session"));
    j.Set("id", Json::MakeString(session_id));
    AppendLine(id, j.Dump());
}

void ConversationStore::Rename(const std::string& id, const std::string& title) {
    Json j = Json::MakeObject();
    j.Set("type", Json::MakeString("title"));
    j.Set("title", Json::MakeString(title));
    AppendLine(id, j.Dump());
}

bool ConversationStore::Delete(const std::string& id) { return fsx::RemoveFile(PathOf(id)); }
