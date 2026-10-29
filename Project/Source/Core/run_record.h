#pragma once

#include <string>
#include <vector>

// The outcome of one Claude Code run, kept as <vault>/Runs/<id>.md.
struct RunRecord {
    std::string id;
    std::string time;  // start, "2026-10-28 14:05:09"
    std::string agent_id;
    std::string agent_name;
    std::string task_name;  // empty for chats
    std::string source;     // sohbet / zamanlı / elle / ajan
    std::string status;     // tamamlandı / hata / atlandı / durduruldu
    std::string model;
    long tokens = 0;
    double cost_usd = 0;
    double duration_s = 0;
    std::string conversation_id;
    std::string text;  // the answer, or the reason for a failure / skip

    bool Save(const std::string& runs_dir) const;
    static std::vector<RunRecord> LoadAll(const std::string& runs_dir);  // newest first
};
