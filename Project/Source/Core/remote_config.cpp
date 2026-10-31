#include "Core/remote_config.h"

#include "Core/platform.h"
#include "Core/text.h"

#include <set>

const RemoteInvite* RemoteConfig::Find(const std::string& id) const {
    for (const RemoteInvite& i : invites)
        if (i.id == id)
            return &i;
    return nullptr;
}

RemoteConfig RemoteConfig::Load(const std::string& file) {
    RemoteConfig c;
    std::string content;
    if (!fsx::ReadFile(file, content))
        return c;
    for (const std::string& raw : str::Split(content, '\n')) {
        const std::string line = str::Trim(raw);
        if (!str::StartsWith(line, "Invite="))
            continue;
        const std::vector<std::string> parts = str::Split(line.substr(7), '|');
        if (parts.size() < 4 || parts[0].empty() || parts[2].size() < 16)
            continue;
        if (parts[1] != "viewer" && parts[1] != "editor")
            continue;
        RemoteInvite i;
        i.id = parts[0];
        i.role = parts[1];
        i.token = parts[2];
        i.name = parts[3];
        c.invites.push_back(i);
    }
    return c;
}

bool RemoteConfig::Save(const std::string& file) const {
    std::string out = "[Remote]\n";
    for (const RemoteInvite& i : invites) {
        std::string name = i.name;
        for (char& ch : name)
            if (ch == '|' || ch == '\n' || ch == '\r')
                ch = ' ';
        out += "Invite=" + i.id + "|" + i.role + "|" + i.token + "|" + name + "\n";
    }
    return fsx::WriteFile(file, out);
}

bool RemoteRoleAllows(const std::string& role, const std::string& cmd) {
    static const char* const owner_only[] = {"vault.", "telegram.", "settings.", "scheduler.", "remote."};
    for (const char* prefix : owner_only)
        if (cmd.rfind(prefix, 0) == 0)
            return false;
    if (role == "editor")
        return true;
    static const std::set<std::string> read_only = {"bootstrap", "chats.list", "chat.load", "chat.export",
                                                    "runs.list", "run.get",    "templates.list"};
    return role == "viewer" && read_only.count(cmd) > 0;
}
