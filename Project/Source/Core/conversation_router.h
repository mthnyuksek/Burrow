#pragma once

#include "Core/conversation.h"
#include "Core/job_builder.h"
#include "Core/run_manager.h"

#include <functional>

// Turns finished runs into conversation entries and follow-up runs:
//  - the answer is written to the conversation of the job's receiver (the user's chat, or the
//    conversation with the agent that asked),
//  - <send to="..."> blocks start (or continue) a conversation between agents,
//  - when an agent was asked something, its answer is ALWAYS handed back to the asker, who then
//    reports to whoever asked them (e.g. the user's chat). Only <send ... reply="no"> and
//    kMaxAgentHops stop that.
class ConversationRouter {
public:
    ConversationRouter(ConversationStore* store, RunManager* runs) : store_(store), runs_(runs) {}

    void HandleResult(const TeamContext& team, const RunResult& result);

    // Called for every <telegram> block of an agent that has the telegram_bildirim skill.
    std::function<void(const AgentDefinition& from, const std::string& text)> on_telegram;

private:
    void RouteSend(const TeamContext& team, const AgentDefinition& from, const SendDirective& send,
                   const RunJob& job);

    ConversationStore* store_;
    RunManager* runs_;
};
