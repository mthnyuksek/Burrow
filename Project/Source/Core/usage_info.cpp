#include "Core/usage_info.h"

#include <algorithm>

namespace {
void ReadWindow(const Json& j, UsageWindow& w) {
    if (!j.IsObject())
        return;
    w.known = true;
    w.used = std::clamp(j["utilization"].AsNumber(), 0.0, 1.0);
    w.resets_at = static_cast<time_t>(j["resetsAt"].AsNumber());
}
}  // namespace

double UsageInfo::RemainingPercent() const {
    double remaining = -1;
    for (const UsageWindow* w : {&five_hour, &seven_day})
        if (w->known && (remaining < 0 || w->RemainingPercent() < remaining))
            remaining = w->RemainingPercent();
    return remaining;
}

bool UsageInfo::UpdateFromEvent(const Json& event, time_t now) {
    if (event["type"].AsString() != "rate_limit_event")
        return false;
    const Json& windows = event["rate_limit_info"]["unifiedWindows"];
    ReadWindow(windows["five_hour"], five_hour);
    ReadWindow(windows["seven_day"], seven_day);
    if (!Known())
        return false;
    updated = now;
    return true;
}
