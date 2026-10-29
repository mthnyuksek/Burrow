#pragma once

#include <string>
#include <utility>
#include <vector>

// A markdown file with a "key: value" header block between two "---" lines,
// followed by free text. Keys may repeat (their order is kept).
struct MarkdownDoc {
    std::vector<std::pair<std::string, std::string>> fields;
    std::string body;

    static bool Parse(const std::string& text, MarkdownDoc& out);
    std::string Serialize() const;

    void Add(const std::string& key, const std::string& value) { fields.emplace_back(key, value); }
    std::string Get(const std::string& key, const std::string& fallback = std::string()) const;
    int GetInt(const std::string& key, int fallback = 0) const;
    std::vector<std::string> GetAll(const std::string& key) const;
};
