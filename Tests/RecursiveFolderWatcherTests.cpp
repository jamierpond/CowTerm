#include <FolderWatch/RecursiveFolderWatcher.h>

#include <NanoTest/NanoTest.h>
#include <eacp/Core/Threads/EventLoop.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace nano;
using namespace pond::Files;

namespace
{
namespace fs = std::filesystem;

constexpr auto gatherTime = eacp::Time::MS {500};
constexpr auto reportTimeout = eacp::Time::MS {15000};
constexpr auto timeBetweenWrites = eacp::Time::MS {700};
constexpr auto timeForLateReportsToArrive = eacp::Time::MS {2000};
constexpr auto writesInsideExcludedFolders = 6;
constexpr auto filesInABurst = 20;

struct ScratchFolder
{
    explicit ScratchFolder(const std::string& name)
        : path(fs::temp_directory_path()
               / (name + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        fs::create_directories(path / ".git" / "objects");
        fs::create_directories(path / "node_modules" / "left-pad");
        fs::create_directories(path / "src" / "deeper");
    }

    ~ScratchFolder()
    {
        auto error = std::error_code {};
        fs::remove_all(path, error);
    }

    fs::path path;
};

void writeFile(const fs::path& file)
{
    fs::create_directories(file.parent_path());
    auto stream = std::ofstream {file, std::ios::app};
    stream << "changed\n";
}

RecursiveFolderWatcherOptions makeOptions()
{
    auto options = RecursiveFolderWatcherOptions {};
    options.gatherTime = gatherTime;
    options.excludedFolderNames = {".git", "node_modules"};
    return options;
}

// A watched scratch folder recording every call.
struct WatchedScratchFolder
{
    explicit WatchedScratchFolder(const std::string& name)
        : scratch(name)
    {
        watcher.watch({eacp::FilePath {scratch.path}});
    }

    bool hasReported(const fs::path& file) const
    {
        const auto expected = eacp::FilePath {file};

        return std::ranges::any_of(calls,
                                   [&](const auto& call) { return call.contains(expected); });
    }

    // A watch can take a moment to start (Linux adds its watches on its own
    // thread), so the file is written again until its change is reported.
    bool waitForReportWhileWriting(const fs::path& file)
    {
        auto deadline = eacp::Time::Deadline {reportTimeout};

        while (!deadline.expired())
        {
            writeFile(file);

            if (eacp::Threads::runEventLoopUntil([&] { return hasReported(file); }, timeBetweenWrites))
                return true;
        }

        return false;
    }

    void forgetCallsSoFar()
    {
        eacp::Threads::runEventLoopFor(timeForLateReportsToArrive);
        calls.clear();
    }

    ScratchFolder scratch;
    eacp::Vector<eacp::Vector<eacp::FilePath>> calls;
    RecursiveFolderWatcher watcher {[this](const eacp::Vector<eacp::FilePath>& changed) { calls.add(changed); },
                                    makeOptions()};
};
} // namespace

auto tNestedChangeIsReported = test("RecursiveFolderWatcher/aChangeInANestedFolderIsReportedByItsPath") = []
{
    auto watched = WatchedScratchFolder {"folder-watch-nested"};

    check(watched.waitForReportWhileWriting(watched.scratch.path / "src" / "deeper" / "notes.txt"));
};

auto tNewFolderIsWatched = test("RecursiveFolderWatcher/aFolderMadeAfterTheWatchStartedIsWatchedToo") = []
{
    auto watched = WatchedScratchFolder {"folder-watch-new-folder"};
    check(watched.waitForReportWhileWriting(watched.scratch.path / "src" / "live.txt"), "the watch is live");

    fs::create_directories(watched.scratch.path / "made" / "later");
    eacp::Threads::runEventLoopFor(timeBetweenWrites);

    check(watched.waitForReportWhileWriting(watched.scratch.path / "made" / "later" / "notes.txt"));
};

auto tExcludedFoldersAreQuiet = test("RecursiveFolderWatcher/changesInsideExcludedFoldersAreNotReported") = []
{
    auto watched = WatchedScratchFolder {"folder-watch-excluded"};
    check(watched.waitForReportWhileWriting(watched.scratch.path / "src" / "live.txt"), "the watch is live");
    watched.forgetCallsSoFar();

    for (auto write = 0; write < writesInsideExcludedFolders; ++write)
    {
        const auto name = "file-" + std::to_string(write);
        writeFile(watched.scratch.path / ".git" / "objects" / name);
        writeFile(watched.scratch.path / "node_modules" / "left-pad" / name);
        eacp::Threads::runEventLoopFor(timeBetweenWrites);
    }

    eacp::Threads::runEventLoopFor(timeForLateReportsToArrive);
    check(watched.calls.empty());
};

auto tUnwatchedFolderIsQuiet = test("RecursiveFolderWatcher/aFolderNoLongerWatchedIsNotReported") = []
{
    auto watched = WatchedScratchFolder {"folder-watch-dropped"};
    check(watched.waitForReportWhileWriting(watched.scratch.path / "src" / "live.txt"), "the watch is live");

    watched.watcher.watch({});
    watched.forgetCallsSoFar();
    writeFile(watched.scratch.path / "src" / "after.txt");
    eacp::Threads::runEventLoopFor(timeForLateReportsToArrive);

    check(watched.calls.empty());
    check(watched.watcher.getFolders().empty());
};

auto tBurstIsGathered = test("RecursiveFolderWatcher/aBurstOfChangesArrivesInOneOrTwoCalls") = []
{
    constexpr auto mostCallsForABurst = 2;
    auto watched = WatchedScratchFolder {"folder-watch-burst"};
    check(watched.waitForReportWhileWriting(watched.scratch.path / "src" / "live.txt"), "the watch is live");
    watched.forgetCallsSoFar();

    for (auto file = 0; file < filesInABurst; ++file)
        writeFile(watched.scratch.path / "src" / ("burst-" + std::to_string(file)));

    const auto lastFile = watched.scratch.path / "src" / ("burst-" + std::to_string(filesInABurst - 1));
    check(eacp::Threads::runEventLoopUntil([&] { return watched.hasReported(lastFile); }, reportTimeout));
    eacp::Threads::runEventLoopFor(timeForLateReportsToArrive);

    check(watched.calls.size() <= mostCallsForABurst, std::to_string(watched.calls.size()) + " calls");
};
