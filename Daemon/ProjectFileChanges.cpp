#include "ProjectFileChanges.h"

#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/Time.h>

namespace
{
using namespace term::filechanges;

// inotify has no recursive watch, so each subfolder costs a watch of its own.
#if defined(__linux__)
constexpr auto isWatchingEachSubfolder = true;
#else
constexpr auto isWatchingEachSubfolder = false;
#endif

pond::Files::RecursiveFolderWatcherOptions makeWatcherOptions()
{
    auto options = pond::Files::RecursiveFolderWatcherOptions {};

    for (const auto& name: listExcludedFolderNames(isWatchingEachSubfolder))
        options.excludedFolderNames.add(name);

    return options;
}

std::vector<std::string> chooseFoldersToWatch(std::vector<FolderLastInput> folders)
{
    auto ordered = orderFoldersByLastInput(std::move(folders));

    if (isWatchingEachSubfolder)
        return chooseFoldersToWatchOneSubfolderAtATime(ordered);

    return ordered;
}
} // namespace

ProjectFileChanges::ProjectFileChanges()
    : watcher([this](const eacp::Vector<eacp::FilePath>& changed) { noteChanges(changed); }, makeWatcherOptions())
{
}

void ProjectFileChanges::watch(std::vector<FolderLastInput> folders)
{
    watchedFolders = chooseFoldersToWatch(std::move(folders));
    auto paths = eacp::Vector<eacp::FilePath> {};

    for (const auto& folder: watchedFolders)
        paths.add(eacp::FilePath {folder});

    watcher.watch(paths);
}

void ProjectFileChanges::noteChanges(const eacp::Vector<eacp::FilePath>& changed)
{
    const auto now = eacp::Time::sinceEpoch().count;

    for (const auto& path: changed)
    {
        const auto folder = findWatchedFolderOfChange(eacp::toStdPath(path), watchedFolders);

        if (!folder.empty())
            lastChangeByFolder[folder] = now;
    }
}

std::int64_t ProjectFileChanges::getLastChangeMilliseconds(const std::string& folder) const
{
    const auto found = lastChangeByFolder.find(folder);
    return found == lastChangeByFolder.end() ? 0 : found->second;
}
