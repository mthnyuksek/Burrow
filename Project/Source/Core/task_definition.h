#pragma once

#include <string>

enum TaskPriority { kPriorityLow = 0, kPriorityNormal = 1, kPriorityHigh = 2 };

// A reusable job description. Wake-ups of any agent can point at a task, so one
// agent can do different work at different times.
struct TaskDefinition {
    std::string id;  // file name without extension
    std::string name;
    std::string model;  // empty = Claude Code default
    int priority = kPriorityNormal;  // higher priority jobs leave the queue first
    // The task is skipped unless at least this much of the Claude usage is left (0 = always run).
    int min_remaining_percent = 0;
    std::string body;  // what Claude Code is asked to do

    std::string Serialize() const;
    static bool Parse(const std::string& id, const std::string& text, TaskDefinition& out);
};
