#include "RecursiveFolderWatcherPlatform.h"

#include <eacp/Core/Utils/StdPath.h>

#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <filesystem>
#include <map>
#include <set>
#include <thread>

namespace pond::Files::detail
{
namespace
{
namespace fs = std::filesystem;

constexpr auto changesToWatch = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO;
constexpr auto newFolderEvents = IN_CREATE | IN_MOVED_TO;
constexpr auto eventBufferSize = std::size_t {65536};

struct WatchedSubfolder
{
    eacp::FilePath folder;
    fs::path subfolder;
};

// Lives on the watching thread: one inotify watch per folder under each
// watched folder, excluded ones left out.
class SubfolderWatches
{
public:
    SubfolderWatches(int inotifyToUse,
                     const eacp::Vector<std::string>& excludedFolderNamesToUse,
                     const std::atomic<bool>& isStoppingToUse,
                     std::atomic<bool>& hasLoggedWatchLimitToUse)
        : inotify(inotifyToUse)
        , excludedFolderNames(excludedFolderNamesToUse)
        , isStopping(isStoppingToUse)
        , hasLoggedWatchLimit(hasLoggedWatchLimitToUse)
    {
    }

    void addFolderAndSubfolders(const eacp::FilePath& folder, const fs::path& top)
    {
        addOne(folder, top);
        auto error = std::error_code {};
        auto entries = fs::recursive_directory_iterator {top, fs::directory_options::skip_permission_denied, error};

        for (; !error && entries != fs::recursive_directory_iterator {}; entries.increment(error))
        {
            if (hasReachedWatchLimit || isStopping)
                return;

            if (!entries->is_directory(error) || entries->is_symlink(error))
                continue;

            if (isExcludedFolderName(entries->path().filename().string(), excludedFolderNames))
                entries.disable_recursion_pending();
            else
                addOne(folder, entries->path());
        }
    }

    eacp::Vector<eacp::FilePath> readChanges(const char* events, std::size_t length)
    {
        auto changed = eacp::Vector<eacp::FilePath> {};

        for (auto offset = std::size_t {0}; offset < length;)
        {
            const auto& event = *reinterpret_cast<const inotify_event*>(events + offset);
            offset += sizeof(inotify_event) + event.len;
            takeEvent(event, changed);
        }

        return changed;
    }

private:
    void takeEvent(const inotify_event& event, eacp::Vector<eacp::FilePath>& changed)
    {
        if ((event.mask & IN_IGNORED) != 0)
        {
            watchedByDescriptor.erase(event.wd);
            return;
        }

        const auto found = watchedByDescriptor.find(event.wd);

        if (found == watchedByDescriptor.end())
            return;

        const auto watched = found->second;
        const auto path = watched.subfolder / std::string {event.len > 0 ? event.name : ""};
        const auto changedPath = eacp::FilePath {path};

        if (isInsideExcludedFolder(watched.folder, changedPath, excludedFolderNames))
            return;

        if (isNewFolder(event))
            addFolderAndSubfolders(watched.folder, path);

        changed.add(changedPath);
    }

    static bool isNewFolder(const inotify_event& event)
    {
        return (event.mask & IN_ISDIR) != 0 && (event.mask & newFolderEvents) != 0;
    }

    void addOne(const eacp::FilePath& folder, const fs::path& subfolder)
    {
        const auto descriptor = inotify_add_watch(inotify, subfolder.c_str(), changesToWatch);

        if (descriptor >= 0)
        {
            watchedByDescriptor[descriptor] = {folder, subfolder};
            return;
        }

        if (errno == ENOSPC)
            noteWatchLimitReached(subfolder);
    }

    void noteWatchLimitReached(const fs::path& subfolder)
    {
        hasReachedWatchLimit = true;

        if (!hasLoggedWatchLimit.exchange(true))
            eacp::LOG("RecursiveFolderWatcher: reached the inotify watch limit (fs.inotify.max_user_watches) at ",
                      subfolder.string(),
                      "; folders past it are not watched");
    }

    int inotify = -1;
    const eacp::Vector<std::string>& excludedFolderNames;
    const std::atomic<bool>& isStopping;
    std::atomic<bool>& hasLoggedWatchLimit;
    bool hasReachedWatchLimit = false;
    std::map<int, WatchedSubfolder> watchedByDescriptor;
};
} // namespace

// One inotify instance and a thread that adds the watches, then polls it and a
// pipe that stop() writes to, handing each batch of changes to the gatherer.
struct PlatformWatch::Native
{
    Native(ChangeGatherer& gathererToUse, const RecursiveFolderWatcherOptions& options)
        : gatherer(gathererToUse)
        , excludedFolderNames(options.excludedFolderNames)
    {
    }

    ~Native() { stop(); }

    void start()
    {
        inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

        if (inotify < 0 || pipe(wake) != 0)
            return;

        isStopping = false;
        thread = std::thread([this] { watchUntilStopped(); });
    }

    void watchUntilStopped()
    {
        auto watches = SubfolderWatches {inotify, excludedFolderNames, isStopping, hasLoggedWatchLimit};

        for (const auto& folder: folders)
            watches.addFolderAndSubfolders(folder, eacp::toStdPath(folder));

        pollfd waits[] = {{inotify, POLLIN, 0}, {wake[0], POLLIN, 0}};
        auto events = std::vector<char>(eventBufferSize);

        while (poll(waits, 2, -1) >= 0 && (waits[1].revents & POLLIN) == 0)
        {
            const auto length = read(inotify, events.data(), events.size());

            if (length <= 0)
                continue;

            const auto lengthRead = static_cast<std::size_t>(length);
            auto changed = watches.readChanges(events.data(), lengthRead);

            if (!changed.empty())
                gatherer.addFromAnyThread(std::move(changed));
        }
    }

    void stop()
    {
        isStopping = true;

        if (thread.joinable())
        {
            [[maybe_unused]] auto written = ::write(wake[1], "x", 1);
            thread.join();
        }

        for (auto* descriptor: {&inotify, &wake[0], &wake[1]})
        {
            if (*descriptor >= 0)
                close(*descriptor);

            *descriptor = -1;
        }
    }

    ChangeGatherer& gatherer;
    eacp::Vector<std::string> excludedFolderNames;
    eacp::Vector<eacp::FilePath> folders;
    std::set<std::string> folderPaths;
    int inotify = -1;
    int wake[2] = {-1, -1};
    std::thread thread;
    std::atomic<bool> isStopping = false;
    std::atomic<bool> hasLoggedWatchLimit = false;
};

PlatformWatch::PlatformWatch(ChangeGatherer& gatherer, const RecursiveFolderWatcherOptions& options)
    : native(std::make_unique<Native>(gatherer, options))
{
}

PlatformWatch::~PlatformWatch() = default;

// Adding the watches walks every folder underneath, so the same folders in
// another order keep the watches they have.
void PlatformWatch::watch(const eacp::Vector<eacp::FilePath>& folders)
{
    auto wanted = std::set<std::string> {};

    for (const auto& folder: folders)
        wanted.insert(folder.str());

    if (wanted == native->folderPaths && native->thread.joinable())
        return;

    native->stop();
    native->folders = folders;
    native->folderPaths = wanted;

    if (!folders.empty())
        native->start();
}
} // namespace pond::Files::detail
