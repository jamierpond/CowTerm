#include "PhoneOverview.h"

#include "PhoneConnection.h"

#include "Multiplexer/Protocol.h"
#include "Multiplexer/RemoteHosts.h"
#include "Multiplexer/RemoteLocation.h"

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

std::vector<std::string> listScannedFolders(const RemoteHost& remote)
{
    auto folders = std::vector<std::string> {};

    for (const auto& project: remote.getLastScan())
    {
        if (const auto location = remote::parseRemoteSearchDir(project.path))
            folders.push_back(location->path);
    }

    return folders;
}

void askConnectedHostsToScan(const std::vector<std::string>& hosts)
{
    for (const auto& host: hosts)
    {
        auto* remote = RemoteHosts::find(host);

        if (remote != nullptr && isConnected(*remote))
            remote->requestScan();
    }
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

PaneScreenUnread summarisePaneScreenUnread(const std::vector<overview::OverviewRow>& rows,
                                           const std::string& host,
                                           const std::string& sessionName)
{
    auto summary = PaneScreenUnread {};

    for (const auto& row: rows)
    {
        if (!row.unread)
            continue;

        if (row.host == host && row.sessionName == sessionName)
            summary.unreadPaneIds = row.unreadPaneIds;
        else
            ++summary.unreadSessionsElsewhere;
    }

    return summary;
}

PhoneOverview::PhoneOverview(std::vector<std::string> hostsToUse)
    : hosts(std::move(hostsToUse))
{
}

void PhoneOverview::setShown(bool isShownNow)
{
    shown = isShownNow;

    if (shown)
        askConnectedHostsToScan(hosts);

    updateRefreshTimer();
}

void PhoneOverview::setPaneScreenShown(bool isShownNow)
{
    paneScreenShown = isShownNow;
    updateRefreshTimer();
}

bool PhoneOverview::isAskingForRows() const
{
    return shown || paneScreenShown;
}

void PhoneOverview::updateRefreshTimer()
{
    refreshTimer.reset();

    if (!isAskingForRows())
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
    if (isAskingForRows())
        report();
}

void PhoneOverview::handleActivity(const std::string& host, const std::string& payload)
{
    activityByHost[host] = overview::parseActivityReport(payload);

    if (isAskingForRows())
        report();
}

std::optional<std::string> PhoneOverview::findPaneToOpen(const overview::OverviewRow& row) const
{
    auto* remote = RemoteHosts::find(row.host);

    if (remote == nullptr)
        return std::nullopt;

    const auto listed = remote->getDaemon().listShellIds();

    if (const auto unread = findNewestUnreadListedPane(row, listed))
        return unread;

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

std::optional<std::string> PhoneOverview::findNewestUnreadListedPane(const overview::OverviewRow& row,
                                                                    const std::vector<std::string>& listed) const
{
    auto newest = std::optional<std::string> {};
    auto newestSince = std::int64_t {0};

    for (const auto& paneId: row.unreadPaneIds)
    {
        if (std::ranges::find(listed, paneId) == listed.end())
            continue;

        const auto since = findActivity(row.host, paneId).unreadSinceMilliseconds;

        if (!newest || since > newestSince)
        {
            newest = paneId;
            newestSince = since;
        }
    }

    return newest;
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
    auto& remote = *RemoteHosts::find(host);
    auto& daemon = remote.getDaemon();
    auto hostPanes = overview::HostPanes {host, {}, listScannedFolders(remote)};

    for (const auto& paneId: removeDiffViewPaneIds(daemon.listShellIds()))
    {
        const auto info = daemon.infoFor(paneId);
        hostPanes.panes.push_back({paneId,
                                   info.foregroundProcess,
                                   info.sessionName,
                                   findActivity(host, paneId),
                                   info.workingDirectory});
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
    const auto built = buildReport();

    if (shown)
        onRowsChanged(built);

    if (paneScreenShown)
        onRowsChangedUnderPaneScreen(built);
}
} // namespace term::phone
