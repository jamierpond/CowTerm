#include "RecursiveFolderWatcherPlatform.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <algorithm>
#include <filesystem>

namespace pond::Files
{
namespace detail
{
bool isExcludedFolderName(std::string_view name, const eacp::Vector<std::string>& excludedFolderNames)
{
    return std::ranges::any_of(excludedFolderNames,
                               [&](const std::string& pattern)
                               { return !pattern.empty() && eacp::Files::matchesWildcard(name, pattern); });
}

bool isInsideExcludedFolder(const eacp::FilePath& watchedFolder,
                            const eacp::FilePath& changed,
                            const eacp::Vector<std::string>& excludedFolderNames)
{
    const auto changedPath = eacp::toStdPath(changed);
    const auto inside = changedPath.lexically_relative(eacp::toStdPath(watchedFolder));

    if (inside.empty())
        return false;

    const auto isFolderExcluded = [&](const std::filesystem::path& part)
    { return isExcludedFolderName(part.string(), excludedFolderNames); };

    const auto parents = inside.parent_path();

    if (std::ranges::any_of(parents, isFolderExcluded))
        return true;

    auto error = std::error_code {};
    return isFolderExcluded(inside.filename()) && std::filesystem::is_directory(changedPath, error);
}

ChangeGatherer::ChangeGatherer(const RecursiveFolderWatcher::ChangedFunc& onChangedToUse,
                               eacp::Time::MS gatherTimeToUse)
    : onChanged(onChangedToUse)
    , gatherTime(gatherTimeToUse)
{
}

void ChangeGatherer::add(const eacp::Vector<eacp::FilePath>& changed)
{
    for (const auto& path: changed)
    {
        if (pendingPaths.insert(path.str()).second)
            pending.add(path);
    }

    if (pending.empty() || isReportScheduled)
        return;

    isReportScheduled = true;
    eacp::Threads::callAfter(gatherTime,
                             [this, guard = std::weak_ptr<bool> {alive}]
                             {
                                 if (!guard.expired())
                                     report();
                             });
}

// The gatherer dies on the main thread, where this runs, so the check cannot race.
void ChangeGatherer::addFromAnyThread(eacp::Vector<eacp::FilePath> changed)
{
    eacp::Threads::callAsync(
        [this, guard = std::weak_ptr<bool> {alive}, changed = std::move(changed)]
        {
            if (!guard.expired())
                add(changed);
        });
}

void ChangeGatherer::report()
{
    isReportScheduled = false;
    const auto changed = std::exchange(pending, {});
    pendingPaths.clear();
    onChanged(changed);
}
} // namespace detail

struct RecursiveFolderWatcher::Impl
{
    Impl(const ChangedFunc& onChanged, const RecursiveFolderWatcherOptions& options)
        : gatherer(onChanged, options.gatherTime)
        , platform(gatherer, options)
    {
    }

    eacp::Vector<eacp::FilePath> folders;
    detail::ChangeGatherer gatherer;
    detail::PlatformWatch platform;
};

RecursiveFolderWatcher::RecursiveFolderWatcher(const ChangedFunc& onChanged,
                                               const RecursiveFolderWatcherOptions& options)
    : impl(onChanged, options)
{
}

RecursiveFolderWatcher::~RecursiveFolderWatcher() = default;

void RecursiveFolderWatcher::watch(const eacp::Vector<eacp::FilePath>& folders)
{
    impl->folders = folders;
    impl->platform.watch(folders);
}

const eacp::Vector<eacp::FilePath>& RecursiveFolderWatcher::getFolders() const
{
    return impl->folders;
}
} // namespace pond::Files
