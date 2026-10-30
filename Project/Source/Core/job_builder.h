#pragma once

#include "Core/agent_definition.h"
#include "Core/project_definition.h"
#include "Core/rule_definition.h"
#include "Core/run_manager.h"
#include "Core/skill_definition.h"
#include "Core/task_definition.h"

#include <vector>

// Everything a job needs to know about the team.
struct TeamContext {
    std::vector<AgentDefinition> agents;
    std::vector<TaskDefinition> tasks;
    std::vector<SkillDefinition> skills;
    std::vector<RuleDefinition> rules;
    std::vector<ProjectDefinition> projects;  // with their notes, read fresh from disk when the team is built
    std::string vault_root;

    const AgentDefinition* FindAgent(const std::string& id) const;
    const TaskDefinition* FindTask(const std::string& id) const;
    const ProjectDefinition* FindProject(const std::string& id) const;
};

// Maximum number of agent-to-agent steps in one chain; a safety net against endless
// back and forth (every step costs tokens). A user -> A -> B -> A report chain needs 3.
constexpr int kMaxAgentHops = 8;

// The limit in force: kMaxAgentHops unless the vault settings (MaxHops=) say otherwise.
inline int& MaxAgentHops() {
    static int limit = kMaxAgentHops;
    return limit;
}

// Every job can belong to a project (project_id, empty = none). The project's summary and its
// enabled notes are then added to the system prompt, so the agent knows the context without
// being reminded. An unknown project id is ignored.

// One message from the user to an agent, continuing session_id if there is one.
// name_chat: the chat has no name yet (first message); the agent is asked to start its answer with
// <title>short name</title>, which the router turns into the chat's title.
RunJob MakeChatJob(const TeamContext& team, const AgentDefinition& agent, const std::string& conversation_id,
                   const std::string& session_id, const std::string& user_text,
                   const std::string& project_id = std::string(), bool name_chat = false);

// A wake-up or manual start. task may be null: the agent then follows its own persona.
RunJob MakeTaskJob(const TeamContext& team, const AgentDefinition& agent, const TaskDefinition* task,
                   JobSource source, const std::string& project_id = std::string());

// A message that `from` sent to `to`. reply_stack already ends with the frame {from, conversation}
// to which the answer will go.
RunJob MakeAgentMessageJob(const TeamContext& team, const AgentDefinition& to, const AgentDefinition& from,
                           const std::string& text, const std::vector<ReplyFrame>& reply_stack, int hops,
                           bool expects_reply, const std::string& project_id = std::string());

// `agent` asked `from` for something and now receives the result. Its own answer goes to
// the receiver at the end of reply_stack (the user's chat, or the agent that asked `agent`).
// A non-empty session_id continues the user's chat session so `agent` still knows the question.
RunJob MakeReportJob(const TeamContext& team, const AgentDefinition& agent, const AgentDefinition& from,
                     const std::string& result, const std::vector<ReplyFrame>& reply_stack,
                     const std::string& session_id, int hops, const std::string& project_id = std::string());

struct SendDirective {
    std::string to;  // agent id
    std::string title;  // <send to="x" title="Kısa ad">: name of the conversation this opens (may be empty)
    std::string text;
    bool expects_reply = true;  // <send to="x" reply="no"> means information only
};

struct ParsedReply {
    std::string title;  // from <title>..</title>, empty if there was none
    std::string text;  // the answer without <send> and <title> blocks
    std::vector<SendDirective> sends;
    std::vector<std::string> telegram;  // <telegram>..</telegram> blocks: messages for the user's Telegram chat
};

// Skill id that lets an agent message the user on Telegram (see kTelegramSkillId in the router).
constexpr const char* kTelegramSkillId = "telegram_bildirim";

// Finds <send to="agent_id">...</send> and <telegram>...</telegram> blocks in an agent's answer.
ParsedReply ParseReply(const std::string& raw);
