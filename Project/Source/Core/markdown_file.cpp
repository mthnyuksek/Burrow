#include "Core/markdown_file.h"

#include "Core/text.h"

bool MarkdownDoc::Parse(const std::string& raw, MarkdownDoc& out) {
    const std::string normalized = str::ReplaceAll(raw, "\r\n", "\n");
    if (!str::StartsWith(normalized, "---\n"))
        return false;
    const size_t end = normalized.find("\n---\n", 3);
    if (end == std::string::npos)
        return false;

    MarkdownDoc doc;
    const std::string header = normalized.substr(4, end - 4 + 1);
    for (const std::string& line : str::Split(header, '\n')) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        doc.Add(str::Trim(line.substr(0, colon)), str::Trim(line.substr(colon + 1)));
    }
    doc.body = normalized.substr(end + 5);
    out = doc;
    return true;
}

std::string MarkdownDoc::Serialize() const {
    std::string s = "---\n";
    for (const auto& f : fields)
        s += f.first + ": " + f.second + "\n";
    s += "---\n" + body;
    if (!body.empty() && body.back() != '\n')
        s += "\n";
    return s;
}

std::string MarkdownDoc::Get(const std::string& key, const std::string& fallback) const {
    for (const auto& f : fields)
        if (f.first == key)
            return f.second;
    return fallback;
}

int MarkdownDoc::GetInt(const std::string& key, int fallback) const {
    long n = 0;
    return str::ToLong(Get(key), n) ? static_cast<int>(n) : fallback;
}

std::vector<std::string> MarkdownDoc::GetAll(const std::string& key) const {
    std::vector<std::string> values;
    for (const auto& f : fields)
        if (f.first == key)
            values.push_back(f.second);
    return values;
}
