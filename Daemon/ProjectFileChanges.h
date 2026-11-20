#pragma once

#include "Multiplexer/FileChangeRules.h"
#include <FolderWatch/RecursiveFolderWatcher.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// When a file last changed in each pane's project folder, for the overview to
// put the busiest project first. A folder no longer watched keeps its time.
class ProjectFileChanges
{
public:
    ProjectFileChanges();

    void watch(std::vector<term::filechanges::FolderLastInput> folders);

    // Milliseconds since 1970, 0 when no change was seen.
    std::int64_t getLastChangeMilliseconds(const std::string& folder) const;

private:
    void noteChanges(const eacp::Vector<eacp::FilePath>& changed);

    std::vector<std::string> watchedFolders;
    std::map<std::string, std::int64_t> lastChangeByFolder;
    pond::Files::RecursiveFolderWatcher watcher;
};
