#include "Core/scheduler.h"

#include "Core/platform.h"
#include "Core/text.h"

#include <cstdio>

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
            if (!w.date.empty()) {
                // One-time: stays due for the grace window until the caller ran it and removed it.
                int y = 0, mo = 0, d = 0;
                long oh = 0, om = 0;
                if (std::sscanf(w.date.c_str(), "%d-%d-%d", &y, &mo, &d) != 3 ||
                    !str::ToLong(str::BeforeFirst(w.time, ':'), oh) || !str::ToLong(str::AfterFirst(w.time, ':'), om))
                    continue;
                const time_t at = timeutil::Make(y, mo, d, static_cast<int>(oh), static_cast<int>(om), 0);
                if (now >= at && now - at < static_cast<time_t>(kGraceMinutes) * 60 && at >= reset_at_)
                    due.push_back({agent.id, w});
                continue;
            }
            if (!w.RunsOn(timeutil::Weekday(now)))
                continue;
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
