#pragma once

#include <string>
#include <vector>

// The list of vaults this user has opened, kept in %APPDATA%\burrow\vaults.ini.
class VaultRegistry {
public:
    struct Entry {
        std::string name;
        std::string path;
    };

    VaultRegistry();

    const std::vector<Entry>& Entries() const { return entries_; }
    std::string LastOpenedPath() const { return last_opened_; }

    void Add(const std::string& name, const std::string& path);
    void Remove(const std::string& path);
    void SetLastOpened(const std::string& path);

private:
    void Load();
    void Save() const;

    std::string file_;
    std::vector<Entry> entries_;
    std::string last_opened_;
};
