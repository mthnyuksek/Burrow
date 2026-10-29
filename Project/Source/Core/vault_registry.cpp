#include "Core/vault_registry.h"

#include "Core/platform.h"
#include "Core/text.h"

#include <map>

namespace {
// The file keeps the layout of earlier versions (backslashes doubled), so their lists still load.
std::string Escape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string Unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out += s[i];
            continue;
        }
        switch (s[++i]) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            default: out += s[i];  // "\\" and anything else: the character itself
        }
    }
    return out;
}
}  // namespace

VaultRegistry::VaultRegistry() {
    const std::string dir = fsx::UserDataDir();
    file_ = fsx::Join(dir, "vaults.ini");
    // The program used to be called "otomation": take over its vault list on the first start.
    const std::string old_file = fsx::Join(fsx::Join(fsx::ParentOf(dir), "otomation"), "vaults.ini");
    if (!fsx::FileExists(file_) && fsx::FileExists(old_file))
        fsx::Copy(old_file, file_);
    Load();
}

void VaultRegistry::Load() {
    std::string content;
    if (!fsx::ReadFile(file_, content))
        return;
    if (str::StartsWith(content, "\xEF\xBB\xBF"))
        content.erase(0, 3);

    std::map<std::string, std::map<std::string, std::string>> sections;
    std::string section;
    for (const std::string& raw : str::Split(content, '\n')) {
        const std::string line = str::Trim(raw);
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        const size_t eq = line.find('=');
        if (eq != std::string::npos)
            sections[section][str::Trim(line.substr(0, eq))] = Unescape(str::Trim(line.substr(eq + 1)));
    }

    last_opened_ = sections["General"]["LastOpened"];
    long count = 0;
    str::ToLong(sections["General"]["Count"], count);
    for (long i = 0; i < count; ++i) {
        auto& group = sections["Vault" + std::to_string(i)];
        Entry e{group["Name"], group["Path"]};
        if (!e.path.empty())
            entries_.push_back(e);
    }
}

void VaultRegistry::Save() const {
    std::string out = "[General]\nLastOpened=" + Escape(last_opened_) + "\nCount=" +
                      std::to_string(entries_.size()) + "\n";
    for (size_t i = 0; i < entries_.size(); ++i)
        out += "[Vault" + std::to_string(i) + "]\nName=" + Escape(entries_[i].name) + "\nPath=" +
               Escape(entries_[i].path) + "\n";
    fsx::WriteFile(file_, out);
}

void VaultRegistry::Add(const std::string& name, const std::string& path) {
    for (Entry& e : entries_) {
        if (str::CmpNoCase(e.path, path) == 0) {
            e.name = name;
            Save();
            return;
        }
    }
    entries_.push_back({name, path});
    Save();
}

void VaultRegistry::Remove(const std::string& path) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (str::CmpNoCase(it->path, path) == 0) {
            entries_.erase(it);
            break;
        }
    }
    if (str::CmpNoCase(last_opened_, path) == 0)
        last_opened_.clear();
    Save();
}

void VaultRegistry::SetLastOpened(const std::string& path) {
    last_opened_ = path;
    Save();
}
