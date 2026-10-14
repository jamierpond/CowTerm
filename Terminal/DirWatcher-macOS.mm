#include "DirWatcher.h"

#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <unistd.h>

namespace term
{
struct DirWatcher::Native
{
    ~Native() { stop(); }

    void stop()
    {
        for (auto source: sources)
        {
            dispatch_source_cancel(source);
            dispatch_release(source);
        }

        sources.clear();
    }

    std::vector<dispatch_source_t> sources;
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
        const auto fd = open(dir.c_str(), O_EVTONLY | O_DIRECTORY);

        if (fd < 0)
            continue;

        auto source = dispatch_source_create(DISPATCH_SOURCE_TYPE_VNODE,
                                             (uintptr_t) fd,
                                             DISPATCH_VNODE_WRITE | DISPATCH_VNODE_DELETE
                                                 | DISPATCH_VNODE_RENAME,
                                             dispatch_get_main_queue());

        if (source == nullptr)
        {
            close(fd);
            continue;
        }

        // The handler reads onChange through `this`; the sources are
        // cancelled (and their handlers stop) before the watcher dies.
        dispatch_source_set_event_handler(source, ^{ onChange(); });
        dispatch_source_set_cancel_handler(source, ^{ close(fd); });
        dispatch_resume(source);
        native->sources.push_back(source);
    }
}
} // namespace term
