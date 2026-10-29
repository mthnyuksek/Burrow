#include "Core/scheduler.h"

#include "Core/platform.h"
#include "Core/text.h"

void Scheduler::Reset(time_t now) {
    last_fired_.clear();
    reset_at_ = now;
}

std::vector<Scheduler::Due> Scheduler::Collect(const std::vector<AgentDefinition>& agents, time_t now) {
    std::vector<Due> due;
    for (const AgentDefinition& agent : agents) {
        if (!agent.enabled)
            continue;
        for (const Wakeup& w : agent.wakeups) {
            const std::string key = agent.id + "#" + w.Serialize();
            auto last = last_fired_.find(key);
            if (w.interval_minutes > 0) {
                const time_t since = last != last_fired_.end() ? last->second : reset_at_;
                if (now - since >= static_cast<time_t>(w.interval_minutes) * 60) {
                    last_fired_[key] = now;
                    due.push_back({agent.id, w});
                }
                continue;
            }
            long h = 0, m = 0;
            if (!str::ToLong(str::BeforeFirst(w.time, ':'), h) || !str::ToLong(str::AfterFirst(w.time, ':'), m))
                continue;
            const time_t target = timeutil::SetClock(now, static_cast<int>(h), static_cast<int>(m));
            const bool in_window = now >= target && now - target < static_cast<time_t>(kGraceMinutes) * 60 &&
                                   target >= reset_at_;
            const bool fired_today = last != last_fired_.end() && timeutil::SameDate(last->second, now);
            if (in_window && !fired_today) {
                last_fired_[key] = now;
                due.push_back({agent.id, w});
            }
        }
    }
    return due;
}
