#pragma once

#include <functional>
#include <memory>

namespace cowterm::mobile
{
// Calls onPathChanged on the main thread whenever the phone's network path
// changes (Wi-Fi to cellular, a VPN coming up), through NWPathMonitor. eacp
// has no network-change event, so this is CowTerm's own glue.
class NetworkPathWatch
{
public:
    explicit NetworkPathWatch(std::function<void()> onPathChanged);
    ~NetworkPathWatch();

    struct Monitor;

private:
    std::unique_ptr<Monitor> monitor;
};
} // namespace cowterm::mobile
