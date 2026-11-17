#include "NetworkPathWatch.h"

#import <Network/Network.h>

#include <eacp/Core/Threads/Async.h>

namespace cowterm::mobile
{
struct NetworkPathWatch::Monitor
{
    nw_path_monitor_t pathMonitor = nw_path_monitor_create();
};

NetworkPathWatch::NetworkPathWatch(std::function<void()> onPathChanged)
    : monitor(std::make_unique<Monitor>())
{
    // NWPathMonitor reports the current path once as it starts; only later
    // reports are changes.
    auto isFirstReport = std::make_shared<bool>(true);

    nw_path_monitor_set_update_handler(monitor->pathMonitor, ^(nw_path_t) {
        if (std::exchange(*isFirstReport, false))
            return;

        eacp::Threads::callAsync(onPathChanged);
    });

    nw_path_monitor_set_queue(monitor->pathMonitor, dispatch_get_main_queue());
    nw_path_monitor_start(monitor->pathMonitor);
}

NetworkPathWatch::~NetworkPathWatch()
{
    nw_path_monitor_cancel(monitor->pathMonitor);
}
} // namespace cowterm::mobile
