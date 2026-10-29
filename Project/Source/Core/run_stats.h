#pragma once

#include "Core/run_record.h"

#include <map>
#include <vector>

// What one agent has used, counted from the run records (no Claude call needed).
struct AgentUsage {
    long tokens_today = 0;
    long tokens_7d = 0;
    int runs_today = 0;
    int runs_7d = 0;
    int failures_7d = 0;
    std::string last_run;  // start time of the newest run, empty if none
};

// today is "YYYY-MM-DD"; the 7 day window is the last 7 calendar days including today.
// Records without an agent (usage probes) are ignored.
std::map<std::string, AgentUsage> ComputeUsage(const std::vector<RunRecord>& records, const std::string& today);

// The date 6 days before `today` ("YYYY-MM-DD"), the first day of the 7 day window.
std::string WeekStart(const std::string& today);

// A daily token limit of 0 means unlimited.
inline bool OverDailyLimit(long tokens_today, int daily_limit) {
    return daily_limit > 0 && tokens_today >= daily_limit;
}
