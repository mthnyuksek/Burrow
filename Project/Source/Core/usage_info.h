#pragma once

#include "Core/json.h"

#include <ctime>

// How much of the Claude subscription is used, as reported by Claude Code's
// "rate_limit_event" messages (5-hour and 7-day windows).
struct UsageWindow {
    bool known = false;
    double used = 0;  // 0..1
    time_t resets_at = 0;

    double RemainingPercent() const { return (1.0 - used) * 100.0; }
};

struct UsageInfo {
    UsageWindow five_hour;
    UsageWindow seven_day;
    time_t updated = 0;  // when we last heard from Claude Code, 0 = never

    bool Known() const { return five_hour.known || seven_day.known; }
    // The tighter of the known windows, in percent. -1 when nothing is known.
    double RemainingPercent() const;

    // Reads one stream-json message; returns false if it is not a usage event.
    bool UpdateFromEvent(const Json& event, time_t now);
};
