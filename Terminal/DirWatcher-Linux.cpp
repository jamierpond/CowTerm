#include "DirWatcher.h"

#include <eacp/Core/Threads/EventLoop.h>

#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <thread>

namespace term
{
namespace
{
// Entries appearing, going and being renamed, plus the directory itself going
// away: the same events the macOS vnode source asks for.
constexpr auto childNameChanges = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO
                                  | IN_DELETE_SELF | IN_MOVE_SELF;
} // namespace

// One inotify instance holds a watch per directory; a thread polls it and a
// pipe that stop() writes to, and hands each change to the main thread.
struct DirWatcher::Native
{
    ~Native() { stop(); }

    void start(DirWatcher& owner)
    {
        if (pipe(wake) != 0)
        {
            wake[0] = wake[1] = -1;
            return;
        }

        thread = std::thread(
            [this, &owner, alive = std::weak_ptr<bool> {alive}]
            {
                pollfd fds[] = {{inotify, POLLIN, 0}, {wake[0], POLLIN, 0}};
                char buffer[4096];

                while (poll(fds, 2, -1) >= 0 && (fds[1].revents & POLLIN) == 0)
                {
                    if ((fds[0].revents & POLLIN) == 0
                        || read(inotify, buffer, sizeof buffer) <= 0)
                        continue;

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
            [[maybe_unused]] auto written = write(wake[1], "x", 1);
            thread.join();
        }

        for (auto* fd: {&inotify, &wake[0], &wake[1]})
        {
            if (*fd >= 0)
                close(*fd);

            *fd = -1;
        }

        alive = std::make_shared<bool>(true);
    }

    int inotify = -1;
    int wake[2] = {-1, -1};
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
    native->inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

    if (native->inotify < 0)
        return;

    auto watching = false;

    for (const auto& dir: dirs)
        watching =
            inotify_add_watch(native->inotify, dir.c_str(), childNameChanges) >= 0
            || watching;

    if (watching)
        native->start(*this);
}
} // namespace term
