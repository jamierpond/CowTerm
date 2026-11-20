#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Which folders the daemon watches for file changes, and which changes count.
namespace term::filechanges
{
// The folder a pane's project is in: the nearest folder at or above where the
// pane started that holds a .git. Empty outside a git repository (~/projects,
// ~/Library), and for a user's home folder or a filesystem root even when it
// is one: watching those would report changes from everywhere.
std::string findProjectFolder(const std::filesystem::path& workingDirectory,
                              const std::filesystem::path& homeFolder);

bool isTooBroadToWatch(const std::filesystem::path& folder,
                       const std::filesystem::path& homeFolder);

struct FolderLastInput
{
    std::string folder;
    std::int64_t lastInputMilliseconds = 0;
};

// Each folder once, the one typed into last first; empty folders left out.
std::vector<std::string> orderFoldersByLastInput(std::vector<FolderLastInput> folders);

// Where a watch is needed per subfolder (inotify), only the folders typed into
// most recently are watched.
std::vector<std::string>
    chooseFoldersToWatchOneSubfolderAtATime(const std::vector<std::string>& foldersByLastInput);

// The folder names whose changes never count: .git, so a background `git
// fetch` does not make an idle project look busy. Where each subfolder needs a
// watch of its own (inotify), also the ones conventionally huge and generated
// (node_modules, build*, _deps, target, .venv).
std::vector<std::string> listExcludedFolderNames(bool isWatchingEachSubfolder);

// The watched folder a changed path is in, the innermost when they nest;
// empty when it is in none.
std::string findWatchedFolderOfChange(const std::filesystem::path& changed,
                                      const std::vector<std::string>& watchedFolders);
} // namespace term::filechanges
