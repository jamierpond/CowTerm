#include "PhoneOverview.h"

#include "PhoneConnection.h"

#include "Multiplexer/Protocol.h"
#include "Multiplexer/RemoteHosts.h"

#include <eacp/Core/Threads/Async.h>

#include <algorithm>

namespace term::phone
{
using namespace eacp;

namespace
{
constexpr auto overviewRefreshInterval = Time::MS {3000};

bool isConnected(const RemoteHost& remote)
{
    return remote.getStatus() == RemoteHost::Status::Connected;
}
} // namespace

std::optional<overview::OverviewRow> findRowByName(const std::vector<overview::OverviewRow>& rows,
                                                   const std::string& hostAndSession)
{
    for (const auto& row: rows)
    {
        if (row.host + "/" + row.sessionName == hostAndSession)
            return row;
    }

    return std::nullopt;
}

PhoneOverview::PhoneOverview(std::vector<std::string> hostsToUse)
    : hosts(std::move(hostsToUse))
{
}

void PhoneOverview::setShown(bool isShownNow)
{
    shown = isShownNow;
    refreshTimer.reset();

    if (!shown)
        return;

    refresh();
    refreshTimer = std::make_unique<Threads::Timer>(
        [this, guard = std::weak_ptr<bool> {alive}]
        {
            Threads::callAsync([this, guard]
                               {
                                   if (!guard.expired())
                                       refresh();
                               });
        },
        overviewRefreshInterval);
}

void PhoneOverview::refresh()
{
    report();

    for (const auto& host: hosts)
    {
        auto* remote = RemoteHosts::find(host);

        if (remote == nullptr || !isConnected(*remote))
            continue;

        remote->getDaemon().send(proto::make("info"));
        remote->getDaemon().send(proto::make("activity"));
    }
}

void PhoneOverview::handleInfoChanged()
{
    if (shown)
        report();
}

void PhoneOverview::handleActivity(const std::string& host, const std::string& payload)
{
    activityByHost[host] = overview::parseActivityReport(payload);

    if (shown)
        report();
}

std::optional<std::string> PhoneOverview::findPaneToOpen(const overview::OverviewRow& row) const
{
    auto* remote = RemoteHosts::find(row.host);

    if (remote == nullptr)
        return std::nullopt;

    const auto listed = remote->getDaemon().listShellIds();
    auto best = std::optional<std::string> {};
    auto bestLastInput = std::int64_t {0};

    for (const auto& paneId: row.paneIds)
    {
        if (std::ranges::find(listed, paneId) == listed.end())
            continue;

        const auto lastInput = findActivity(row.host, paneId).lastInputMilliseconds;

        if (!best || lastInput > bestLastInput)
        {
            best = paneId;
            bestLastInput = lastInput;
        }
    }

    return best;
}

OverviewReport PhoneOverview::buildReport() const
{
    auto report = OverviewReport {};
    report.hostCount = static_cast<int>(hosts.size());
    report.nowMillisecondsSinceEpoch = getMillisecondsSinceEpoch();
    auto answeringHosts = std::vector<overview::HostPanes> {};

    for (const auto& host: hosts)
    {
        auto* remote = RemoteHosts::find(host);

        if (remote != nullptr && remote->getStatus() == RemoteHost::Status::Unreachable)
            report.unreachableHosts.push_back({host, remote->getFailureReason()});
        else if (remote != nullptr && isConnected(*remote))
            answeringHosts.push_back(collectHostPanes(host));
    }

    report.sessions = overview::buildOverviewRows(answeringHosts, report.nowMillisecondsSinceEpoch);
    return report;
}

overview::HostPanes PhoneOverview::collectHostPanes(const std::string& host) const
{
    auto& daemon = RemoteHosts::find(host)->getDaemon();
    auto hostPanes = overview::HostPanes {host, {}};

    for (const auto& paneId: daemon.listShellIds())
    {
        const auto info = daemon.infoFor(paneId);
        hostPanes.panes.push_back({paneId, info.foregroundProcess, info.sessionName, findActivity(host, paneId)});
    }

    return hostPanes;
}

overview::PaneActivityReport PhoneOverview::findActivity(const std::string& host, const std::string& paneId) const
{
    const auto found = activityByHost.find(host);

    if (found == activityByHost.end())
        return {};

    for (const auto& activity: found->second)
    {
        if (activity.paneId == paneId)
            return activity;
    }

    return {};
}

void PhoneOverview::report()
{
    onRowsChanged(buildReport());
}
} // namespace term::phone
