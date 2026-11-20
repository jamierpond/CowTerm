#include "RecursiveFolderWatcherPlatform.h"

#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/WinInclude.h>

#include <filesystem>
#include <set>
#include <thread>

namespace pond::Files::detail
{
namespace
{
constexpr auto changesToWatch = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
                                | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
constexpr auto watchesWholeSubtree = TRUE;

// ReadDirectoryChangesW wants a DWORD-aligned buffer, at most 64 KB over a network share.
constexpr auto changeBufferWords = std::size_t {16384};

// One folder's directory handle and the read of its changes in flight.
struct WatchedFolder
{
    eacp::FilePath folder;
    HANDLE directory = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped {};
    std::vector<DWORD> changes = std::vector<DWORD>(changeBufferWords);

    bool hasHandles() const
    {
        return directory != INVALID_HANDLE_VALUE && overlapped.hEvent != nullptr;
    }

    bool startReading()
    {
        const auto bufferBytes = static_cast<DWORD>(changes.size() * sizeof(DWORD));
        return ReadDirectoryChangesW(
                   directory, changes.data(), bufferBytes, watchesWholeSubtree, changesToWatch, nullptr, &overlapped, nullptr)
               != FALSE;
    }

    // A read that overflowed the buffer names nothing; the folder itself stands for it.
    eacp::Vector<eacp::FilePath> readChanges(DWORD bytesRead, const eacp::Vector<std::string>& excludedFolderNames) const
    {
        if (bytesRead == 0)
            return {folder};

        auto changed = eacp::Vector<eacp::FilePath> {};
        const auto* bytes = reinterpret_cast<const std::byte*>(changes.data());

        for (auto offset = DWORD {0};;)
        {
            const auto& change = *reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(bytes + offset);
            const auto nameLength = change.FileNameLength / sizeof(wchar_t);
            const auto path = eacp::FilePath {eacp::toStdPath(folder) / std::wstring {change.FileName, nameLength}};

            if (!isInsideExcludedFolder(folder, path, excludedFolderNames))
                changed.add(path);

            if (change.NextEntryOffset == 0)
                return changed;

            offset += change.NextEntryOffset;
        }
    }

    void close()
    {
        if (directory != INVALID_HANDLE_VALUE)
        {
            CancelIoEx(directory, &overlapped);
            auto bytesRead = DWORD {0};
            GetOverlappedResult(directory, &overlapped, &bytesRead, TRUE);
            CloseHandle(directory);
        }

        if (overlapped.hEvent != nullptr)
            CloseHandle(overlapped.hEvent);
    }
};

std::unique_ptr<WatchedFolder> openFolder(const eacp::FilePath& folder)
{
    auto watched = std::make_unique<WatchedFolder>();
    watched->folder = folder;
    watched->directory = CreateFileW(folder.wide().c_str(),
                                     FILE_LIST_DIRECTORY,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr,
                                     OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                     nullptr);
    watched->overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if (!watched->hasHandles() || !watched->startReading())
    {
        watched->close();
        return nullptr;
    }

    return watched;
}
} // namespace

// One thread waits on every folder's read plus a stop event and hands each
// batch of changes to the gatherer. WaitForMultipleObjects takes at most 64
// handles, so that many folders less the stop event.
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
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        if (stopEvent != nullptr)
            thread = std::thread([this] { watchUntilStopped(); });
    }

    void watchUntilStopped()
    {
        auto handles = std::vector<HANDLE> {stopEvent};

        for (const auto& watched: watchedFolders)
            handles.push_back(watched->overlapped.hEvent);

        const auto handleCount = static_cast<DWORD>(handles.size());

        while (true)
        {
            const auto signalled = WaitForMultipleObjects(handleCount, handles.data(), FALSE, INFINITE);

            if (signalled <= WAIT_OBJECT_0 || signalled >= WAIT_OBJECT_0 + handleCount)
                return;

            takeChanges(*watchedFolders[signalled - WAIT_OBJECT_0 - 1]);
        }
    }

    void takeChanges(WatchedFolder& watched)
    {
        auto bytesRead = DWORD {0};
        const auto hasRead = GetOverlappedResult(watched.directory, &watched.overlapped, &bytesRead, FALSE) != FALSE;
        ResetEvent(watched.overlapped.hEvent);

        auto changed = eacp::Vector<eacp::FilePath> {};

        if (hasRead)
            changed = watched.readChanges(bytesRead, excludedFolderNames);

        watched.startReading();

        if (!changed.empty())
            gatherer.addFromAnyThread(std::move(changed));
    }

    void stop()
    {
        if (thread.joinable())
        {
            SetEvent(stopEvent);
            thread.join();
        }

        if (stopEvent != nullptr)
        {
            CloseHandle(stopEvent);
            stopEvent = nullptr;
        }

        for (auto& watched: watchedFolders)
            watched->close();

        watchedFolders.clear();
        folderPaths.clear();
    }

    ChangeGatherer& gatherer;
    eacp::Vector<std::string> excludedFolderNames;
    std::set<std::string> folderPaths;
    std::vector<std::unique_ptr<WatchedFolder>> watchedFolders;
    HANDLE stopEvent = nullptr;
    std::thread thread;
};

PlatformWatch::PlatformWatch(ChangeGatherer& gatherer, const RecursiveFolderWatcherOptions& options)
    : native(std::make_unique<Native>(gatherer, options))
{
}

PlatformWatch::~PlatformWatch() = default;

void PlatformWatch::watch(const eacp::Vector<eacp::FilePath>& folders)
{
    auto wanted = std::set<std::string> {};

    for (const auto& folder: folders)
        wanted.insert(folder.str());

    if (wanted == native->folderPaths)
        return;

    native->stop();
    native->folderPaths = wanted;

    for (const auto& folder: folders)
    {
        if (native->watchedFolders.size() == MAXIMUM_WAIT_OBJECTS - 1)
            break;

        if (auto watched = openFolder(folder))
            native->watchedFolders.push_back(std::move(watched));
    }

    if (!native->watchedFolders.empty())
        native->start();
}
} // namespace pond::Files::detail
