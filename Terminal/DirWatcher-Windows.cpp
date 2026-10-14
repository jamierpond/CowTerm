#include "DirWatcher.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/WinInclude.h>

#include <thread>

namespace term
{
namespace
{
// A change notification per directory, for names only (FILE_NAME covers files,
// DIR_NAME covers folders, both include renames) and not its subtree: the
// direct children are what the ctrl+a f scan lists.
constexpr auto childNameChanges =
    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME;
} // namespace

// One thread waits on every directory's change handle plus a stop event, and
// hands each change to the main thread. WaitForMultipleObjects takes at most
// 64 handles, so that many directories less the stop event; the search dirs
// are a handful.
struct DirWatcher::Native
{
    ~Native() { stop(); }

    void start(DirWatcher& owner)
    {
        if (changes.empty())
            return;

        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        if (stopEvent == nullptr)
            return;

        thread = std::thread(
            [this, &owner, alive = std::weak_ptr<bool> {alive}]
            {
                auto handles = std::vector<HANDLE> {stopEvent};
                handles.insert(handles.end(), changes.begin(), changes.end());

                while (true)
                {
                    const auto signalled = WaitForMultipleObjects(
                        (DWORD) handles.size(), handles.data(), FALSE, INFINITE);

                    if (signalled <= WAIT_OBJECT_0
                        || signalled >= WAIT_OBJECT_0 + handles.size())
                        return;

                    FindNextChangeNotification(handles[signalled - WAIT_OBJECT_0]);

                    // The watcher can be gone by the time this runs; it dies
                    // on the main thread too, so the check cannot race.
                    eacp::Threads::callAsync(
                        [&owner, alive]
                        {
                            if (!alive.expired())
                                owner.onChange();
                        });
                }
            });
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

        for (auto* change: changes)
            FindCloseChangeNotification(change);

        changes.clear();
        alive = std::make_shared<bool>(true);
    }

    std::vector<HANDLE> changes;
    HANDLE stopEvent = nullptr;
    std::thread thread;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

DirWatcher::DirWatcher()
    : native(std::make_unique<Native>())
{
}

DirWatcher::~DirWatcher() = default;

void DirWatcher::watch(const std::vector<std::string>& dirs)
{
    native->stop();

    for (const auto& dir: dirs)
    {
        if (native->changes.size() == MAXIMUM_WAIT_OBJECTS - 1)
            break;

        auto* change = FindFirstChangeNotificationW(
            eacp::Strings::widen(dir).c_str(), FALSE, childNameChanges);

        if (change != INVALID_HANDLE_VALUE)
            native->changes.push_back(change);
    }

    native->start(*this);
}
} // namespace term
