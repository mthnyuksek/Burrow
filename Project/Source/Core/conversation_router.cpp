#include "Core/conversation_router.h"

#include "Core/text.h"

namespace {
std::string Snippet(const std::string& body, size_t max_length) {
    const std::string one_line = str::Trim(str::ReplaceAll(body, "\n", " "));
    return str::Utf8Length(one_line) > max_length ? str::Utf8Left(one_line, max_length) + "…" : one_line;
}

// A title chosen by an agent: one line, no quotes around it, not longer than a list can show.
std::string CleanTitle(const std::string& title) {
    std::string t = str::Trim(str::ReplaceAll(title, "\n", " "));
    while (!t.empty() && (t.back() == '"' || t.back() == '.'))
        t.pop_back();
    while (!t.empty() && t.front() == '"')
        t.erase(0, 1);
    t = str::Trim(t);
    return str::Utf8Length(t) > 50 ? str::Trim(str::Utf8Left(t, 50)) : t;
}
}  // namespace

void ConversationRouter::RouteSend(const TeamContext& team, const AgentDefinition& from,
                                   const SendDirective& send, const RunJob& job) {
    const AgentDefinition* target = team.FindAgent(send.to);
    std::string note;
    if (!target || target->id == from.id)
        note = "(Gönderilemedi: \"" + send.to + "\" adında bir ajan yok.)";
    else if (!target->enabled)
        note = "(Gönderilemedi: " + target->name + " uykuda.)";
    else if (job.hops + 1 > MaxAgentHops())
        note = "(Zincir sınırına ulaşıldı, mesaj iletilmedi.)";

    const ReplyFrame* receiver = job.reply_stack.empty() ? nullptr : &job.reply_stack.back();
    if (!note.empty()) {
        if (receiver && !receiver->conversation_id.empty())
            store_->Append(receiver->conversation_id, {std::string(), from.id, std::string(), note});
        return;
    }

    // Inside a conversation between agents the talk simply goes on; from a chat or a
    // task run it opens a new conversation between the agents.
    std::string agent_conversation;
    ConversationInfo info;
    std::vector<ConvMessage> messages;
    if (receiver && receiver->agent_id != "user" && !receiver->conversation_id.empty() &&
        store_->Load(receiver->conversation_id, info, messages) && info.kind == "agents") {
        agent_conversation = receiver->conversation_id;
        store_->AddParticipant(agent_conversation, target->id);
    } else {
        const std::string chosen = CleanTitle(send.title);  // the agent that starts the talk names it
        agent_conversation = store_->Create(
            "agents", chosen.empty() ? from.name + " → " + target->name + ": " + Snippet(send.text, 40) : chosen,
            {from.id, target->id}, job.project_id);
    }
    store_->Append(agent_conversation, {std::string(), from.id, target->id, send.text});

    std::vector<ReplyFrame> stack = job.reply_stack;
    stack.push_back({from.id, agent_conversation});  // the answer comes back to `from`
    runs_->Enqueue(MakeAgentMessageJob(team, *target, from, send.text, stack, job.hops + 1, send.expects_reply,
                                       job.project_id));
}

void ConversationRouter::HandleResult(const TeamContext& team, const RunResult& r) {
    const RunJob& job = r.job;
    const bool ok = r.status == RunStatus::Done;
    const AgentDefinition* agent = team.FindAgent(job.agent_id);
    const ReplyFrame* receiver = job.reply_stack.empty() ? nullptr : &job.reply_stack.back();
    const bool to_user = receiver && receiver->agent_id == "user";
    const bool to_agent = receiver && !to_user;

    // The chat goes on in the same Claude session, so the next message is cheap.
    if (to_user && !receiver->conversation_id.empty() && !r.session_id.empty() && job.persist_session)
        store_->SetSession(receiver->conversation_id, r.session_id);

    ParsedReply reply;
    if (ok)
        reply = ParseReply(r.text);

    if (job.name_chat && to_user && !receiver->conversation_id.empty()) {
        const std::string title = CleanTitle(reply.title);
        if (!title.empty())
            store_->Rename(receiver->conversation_id, title);
    }

    if (receiver && !receiver->conversation_id.empty()) {
        const std::string body = ok ? reply.text : "(" + RunStatusLabel(r.status) + ") " + r.text;
        if (!body.empty())
            store_->Append(receiver->conversation_id, {std::string(), job.agent_id, receiver->agent_id, body});
    }
    if (!agent)
        return;

    // A <send> block aimed at the asker is the same as answering, so it is not routed twice.
    std::string answer = reply.text;
    for (const SendDirective& send : reply.sends) {
        if (to_agent && send.to == receiver->agent_id) {
            if (answer.empty()) {
                answer = send.text;
                if (!receiver->conversation_id.empty())
                    store_->Append(receiver->conversation_id,
                                   {std::string(), job.agent_id, receiver->agent_id, answer});
            }
            continue;
        }
        RouteSend(team, *agent, send, job);
    }

    // The asker always receives the result, then reports to whoever asked them.
    if (!to_agent || !job.expects_reply)
        return;
    const AgentDefinition* asker = team.FindAgent(receiver->agent_id);
    std::string report = answer;
    if (r.status == RunStatus::Failed)
        report = "İş tamamlanamadı: " + r.text;
    else if (!ok)
        return;  // skipped or stopped on purpose, nothing to report
    if (!asker || report.empty())
        return;

    if (job.hops + 1 > MaxAgentHops()) {
        if (!receiver->conversation_id.empty())
            store_->Append(receiver->conversation_id,
                           {std::string(), std::string(), std::string(), "(Zincir sınırına ulaşıldı, sonuç iletilmedi.)"});
        return;
    }
    const std::vector<ReplyFrame> rest(job.reply_stack.begin(), job.reply_stack.end() - 1);
    std::string session;
    if (!rest.empty() && rest.back().agent_id == "user" && !rest.back().conversation_id.empty()) {
        ConversationInfo info;
        std::vector<ConvMessage> messages;
        if (store_->Load(rest.back().conversation_id, info, messages))
            session = info.session_id;
    }
    runs_->Enqueue(MakeReportJob(team, *asker, *agent, report, rest, session, job.hops + 1, job.project_id));
}
