#include "RecursiveFolderWatcherPlatform.h"

#include <eacp/Core/Utils/StdPath.h>

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

#include <filesystem>
#include <map>
#include <set>

namespace pond::Files::detail
{
namespace
{
// Short, because the gatherer does the real gathering; it only spares the
// main queue a call per file in a burst.
constexpr auto streamLatencySeconds = CFTimeInterval {0.25};
constexpr auto streamFlags = kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer;

struct WatchedFolder
{
    WatchedFolder(ChangeGatherer& gathererToUse,
                  const eacp::Vector<std::string>& excludedFolderNamesToUse,
                  const eacp::FilePath& folderToUse)
        : gatherer(gathererToUse)
        , excludedFolderNames(excludedFolderNamesToUse)
        , folder(folderToUse)
    {
    }

    ChangeGatherer& gatherer;
    const eacp::Vector<std::string>& excludedFolderNames;
    eacp::FilePath folder;
    std::filesystem::path realFolder;
    FSEventStreamRef stream = nullptr;
};

// FSEvents reports real paths (/private/var for /var); the change is reported
// under the folder as it was given.
eacp::FilePath findPathAsGiven(const WatchedFolder& watched, const char* realPath)
{
    const auto inside = std::filesystem::path {realPath}.lexically_relative(watched.realFolder);

    if (inside.empty() || *inside.begin() == "..")
        return eacp::FilePath {std::filesystem::path {realPath}};

    return eacp::FilePath {eacp::toStdPath(watched.folder) / inside};
}

void reportChanges(ConstFSEventStreamRef,
                   void* info,
                   std::size_t count,
                   void* paths,
                   const FSEventStreamEventFlags*,
                   const FSEventStreamEventId*)
{
    const auto& watched = *static_cast<const WatchedFolder*>(info);
    const auto* pathTexts = static_cast<char**>(paths);
    auto changed = eacp::Vector<eacp::FilePath> {};

    for (auto index = std::size_t {0}; index < count; ++index)
    {
        auto path = findPathAsGiven(watched, pathTexts[index]);

        if (!isInsideExcludedFolder(watched.folder, path, watched.excludedFolderNames))
            changed.add(std::move(path));
    }

    watched.gatherer.add(changed);
}

std::filesystem::path findRealFolder(const eacp::FilePath& folder)
{
    auto error = std::error_code {};
    auto real = std::filesystem::canonical(eacp::toStdPath(folder), error);
    return error ? eacp::toStdPath(folder) : real;
}

void stopStream(FSEventStreamRef stream)
{
    FSEventStreamStop(stream);
    FSEventStreamInvalidate(stream);
    FSEventStreamRelease(stream);
}

FSEventStreamRef createStream(WatchedFolder& watched)
{
    auto context = FSEventStreamContext {0, &watched, nullptr, nullptr, nullptr};
    auto* folderText = CFStringCreateWithCString(nullptr, watched.folder.c_str(), kCFStringEncodingUTF8);
    const void* folderValues[] = {folderText};
    auto* folders = CFArrayCreate(nullptr, folderValues, 1, &kCFTypeArrayCallBacks);

    auto* stream = FSEventStreamCreate(
        nullptr, &reportChanges, &context, folders, kFSEventStreamEventIdSinceNow, streamLatencySeconds, streamFlags);
    CFRelease(folders);
    CFRelease(folderText);
    return stream;
}
} // namespace

// The streams deliver on the main queue, where watch() and the destructor run,
// so a callback never sees a stream being changed.
struct PlatformWatch::Native
{
    Native(ChangeGatherer& gathererToUse, const RecursiveFolderWatcherOptions& options)
        : gatherer(gathererToUse)
        , excludedFolderNames(options.excludedFolderNames)
    {
    }

    ~Native()
    {
        for (auto& [path, watched]: watchedByPath)
            stopStream(watched->stream);
    }

    void start(const eacp::FilePath& folder)
    {
        auto watched = std::make_unique<WatchedFolder>(gatherer, excludedFolderNames, folder);
        watched->realFolder = findRealFolder(folder);
        watched->stream = createStream(*watched);

        if (watched->stream == nullptr)
            return;

        FSEventStreamSetDispatchQueue(watched->stream, dispatch_get_main_queue());

        if (!FSEventStreamStart(watched->stream))
        {
            FSEventStreamInvalidate(watched->stream);
            FSEventStreamRelease(watched->stream);
            return;
        }

        watchedByPath[folder.str()] = std::move(watched);
    }

    ChangeGatherer& gatherer;
    eacp::Vector<std::string> excludedFolderNames;
    std::map<std::string, std::unique_ptr<WatchedFolder>> watchedByPath;
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

    std::erase_if(native->watchedByPath,
                  [&](const auto& entry)
                  {
                      if (wanted.contains(entry.first))
                          return false;

                      stopStream(entry.second->stream);
                      return true;
                  });

    for (const auto& folder: folders)
    {
        if (!native->watchedByPath.contains(folder.str()))
            native->start(folder);
    }
}
} // namespace pond::Files::detail
