#include "Core/run_stats.h"

#include "Core/platform.h"
#include "Core/run_manager.h"

std::string WeekStart(const std::string& today) { return timeutil::DateDaysBefore(today, 6); }

std::map<std::string, AgentUsage> ComputeUsage(const std::vector<RunRecord>& records, const std::string& today) {
    const std::string week_start = WeekStart(today);
    const std::string failed = RunStatusLabel(RunStatus::Failed);
    std::map<std::string, AgentUsage> usage;
    for (const RunRecord& r : records) {
        if (r.agent_id.empty())
            continue;
        const std::string day = r.time.substr(0, 10);
        AgentUsage& u = usage[r.agent_id];
        if (r.time > u.last_run)
            u.last_run = r.time;
        if (day < week_start || day > today)
            continue;
        u.tokens_7d += r.tokens;
        ++u.runs_7d;
        if (r.status == failed)
            ++u.failures_7d;
        if (day == today) {
            u.tokens_today += r.tokens;
            ++u.runs_today;
        }
    }
    return usage;
}
