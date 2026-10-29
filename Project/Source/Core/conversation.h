#pragma once

#include <string>
#include <vector>

struct ConvMessage {
    std::string time;  // "2026-10-28 14:05:09"
    std::string from;  // "user" or an agent id
    std::string to;    // "user", an agent id, or empty
    std::string text;
};

struct ConversationInfo {
    std::string id;
    std::string kind;  // "chat" (user <-> one agent) or "agents" (agents among themselves)
    std::string title;
    std::string project;  // project id, empty = no project
    std::vector<std::string> participants;  // agent ids
    std::string session_id;  // Claude Code session of a chat, empty until the first reply
    std::string updated;     // time of the last message, or creation time
};

// Conversations live in <vault>/Conversations/<id>.jsonl, one JSON object per line:
//   {"type":"meta", ...} first, then {"type":"msg", ...} and {"type":"session","id":...}.
class ConversationStore {
public:
    explicit ConversationStore(const std::string& dir) : dir_(dir) {}

    std::vector<ConversationInfo> List() const;  // newest first
    bool Load(const std::string& id, ConversationInfo& info, std::vector<ConvMessage>& messages) const;

    // Returns the new conversation's id.
    std::string Create(const std::string& kind, const std::string& title,
                    const std::vector<std::string>& participants, const std::string& project = std::string());
    void AddParticipant(const std::string& id, const std::string& agent_id);
    void Append(const std::string& id, const ConvMessage& message);
    void SetSession(const std::string& id, const std::string& session_id);
    void Rename(const std::string& id, const std::string& title);  // the newest title line wins
    bool Delete(const std::string& id);

    static std::string Now();

private:
    std::string PathOf(const std::string& id) const;
    void AppendLine(const std::string& id, const std::string& json_line);

    std::string dir_;
};
