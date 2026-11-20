#pragma once

#include <eacp/Core/Utils/Common.h>
#include <eacp/Core/Utils/FilePath.h>

namespace pond::Files
{
struct RecursiveFolderWatcherOptions
{
    // How long changes gather before they are reported in one call, so a
    // build writing thousands of files costs a handful of calls.
    eacp::Time::MS gatherTime {2000};

    // matchesWildcard patterns (".git", "build*") for folder names at any depth
    // under a watched folder: nothing changing inside a matching folder is
    // reported, and where the platform needs a watch per folder (Linux) none
    // is placed inside one.
    eacp::Vector<std::string> excludedFolderNames;
};

// Watches folders and everything under them with the platform's own change
// events, and reports the files and folders created, written, renamed or
// deleted there. Nothing runs at idle and nothing is polled: macOS has an
// FSEvents stream per folder, Windows a ReadDirectoryChangesW over each
// folder's subtree, and Linux, whose inotify has no recursive mode, a watch
// per folder underneath, added as new folders appear. Linux caps watches per
// user (fs.inotify.max_user_watches); past the cap the rest go unwatched and
// the watcher logs it once.
//
// The callback runs on the main thread and must not destroy the watcher.
class RecursiveFolderWatcher
{
public:
    using ChangedFunc = std::function<void(const eacp::Vector<eacp::FilePath>& changed)>;

    explicit RecursiveFolderWatcher(const ChangedFunc& onChanged,
                                    const RecursiveFolderWatcherOptions& options = {});
    ~RecursiveFolderWatcher();

    RecursiveFolderWatcher(const RecursiveFolderWatcher&) = delete;
    RecursiveFolderWatcher& operator=(const RecursiveFolderWatcher&) = delete;
    RecursiveFolderWatcher(RecursiveFolderWatcher&&) = delete;
    RecursiveFolderWatcher& operator=(RecursiveFolderWatcher&&) = delete;

    // Replaces the watched folders. A folder that does not exist is skipped.
    void watch(const eacp::Vector<eacp::FilePath>& folders);

    const eacp::Vector<eacp::FilePath>& getFolders() const;

private:
    struct Impl;
    eacp::Pimpl<Impl> impl;
};
} // namespace pond::Files
