#pragma once

#include "Core/agent_definition.h"

#include <ctime>
#include <map>
#include <string>
#include <vector>

// Decides which wake-ups are due. It only keeps track of what already fired;
// the caller turns a due wake-up into a job.
class Scheduler {
public:
    struct Due {
        std::string agent_id;
        Wakeup wakeup;
    };

    // Call when the scheduler is switched on: interval wake-ups count from now,
    // and daily wake-ups whose time has already passed today do not fire late.
    void Reset(time_t now);

    std::vector<Due> Collect(const std::vector<AgentDefinition>& agents, time_t now);

private:
    // A daily wake-up fires at most this long after its time (so a late start does not replay the morning).
    static constexpr int kGraceMinutes = 10;

    std::map<std::string, time_t> last_fired_;  // by agent id + wake-up
    time_t reset_at_ = 0;
};
