#pragma once

#include "RecursiveFolderWatcher.h"

#include <memory>
#include <set>

// What the platform files share: the exclusion rule, the gathering of changes
// into one call, and the interface each platform's watch implements.
namespace pond::Files::detail
{
bool isExcludedFolderName(std::string_view name, const eacp::Vector<std::string>& excludedFolderNames);

// True when the change is inside an excluded folder under the watched one, or
// is an excluded folder itself.
bool isInsideExcludedFolder(const eacp::FilePath& watchedFolder,
                            const eacp::FilePath& changed,
                            const eacp::Vector<std::string>& excludedFolderNames);

// Collects changes on the main thread and reports them together once the
// gather time has passed since the first of them.
class ChangeGatherer
{
public:
    ChangeGatherer(const RecursiveFolderWatcher::ChangedFunc& onChangedToUse, eacp::Time::MS gatherTimeToUse);

    void add(const eacp::Vector<eacp::FilePath>& changed);

    // From any thread: hands the changes to add() on the main thread, unless
    // the gatherer is gone by then. Call it only while the gatherer lives.
    void addFromAnyThread(eacp::Vector<eacp::FilePath> changed);

private:
    void report();

    RecursiveFolderWatcher::ChangedFunc onChanged;
    eacp::Time::MS gatherTime;
    eacp::Vector<eacp::FilePath> pending;
    std::set<std::string> pendingPaths;
    bool isReportScheduled = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// One platform's watch of exactly the folders given, reporting to the gatherer.
class PlatformWatch
{
public:
    PlatformWatch(ChangeGatherer& gatherer, const RecursiveFolderWatcherOptions& options);
    ~PlatformWatch();

    PlatformWatch(const PlatformWatch&) = delete;
    PlatformWatch& operator=(const PlatformWatch&) = delete;

    void watch(const eacp::Vector<eacp::FilePath>& folders);

private:
    struct Native;
    std::unique_ptr<Native> native;
};
} // namespace pond::Files::detail
