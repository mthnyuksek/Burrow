#pragma once

#include <string>
#include <vector>

// Friends who may join this Burrow from another computer. Kept per user in %APPDATA%\burrow\remote.ini
// (the tokens are secrets), one line per invite: "Invite=<id>|<role>|<token>|<name>".
// Roles: "viewer" reads only, "editor" may also command agents and create agents, skills, tasks, ...
// The owner (the person at this computer) is never listed: the owner is the app window itself.
struct RemoteInvite {
    std::string id;     // short, shown in the page and used to revoke
    std::string role;   // "viewer" | "editor"
    std::string token;  // long random secret, part of the invite link
    std::string name;   // who it is for
    long last_seen = 0; // not saved; filled while the server runs
};

struct RemoteConfig {
    std::vector<RemoteInvite> invites;

    const RemoteInvite* Find(const std::string& id) const;
    static RemoteConfig Load(const std::string& file);
    bool Save(const std::string& file) const;
};

// What a role may do. Anything that changes the owner's own setup (vaults, settings, Telegram, the
// scheduler, the remote access itself) is for the owner only.
bool RemoteRoleAllows(const std::string& role, const std::string& cmd);
