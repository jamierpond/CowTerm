#include "PhoneOverviewBinding.h"

#include "Multiplexer/RemoteHosts.h"

#include <eacp/Core/Utils/Logging.h>

#include <map>
#include <optional>

namespace cowterm::mobile
{
using namespace eacp;

OverviewInputs makeOverviewInputs(const term::phone::OverviewReport& report,
                                  const term::phone::SessionOpenings& openings)
{
    auto inputs = OverviewInputs {};
    inputs.openingUsage = [&openings, now = report.nowMillisecondsSinceEpoch](const std::string& rowKey)
    { return openings.getUsage(rowKey, now); };
    inputs.lastOpeningOnPhone = [&openings](const std::string& rowKey)
    { return openings.getLastOpeningMilliseconds(rowKey); };
    inputs.sessions = report.sessions;
    inputs.hostCount = report.hostCount;
    inputs.nowMilliseconds = report.nowMillisecondsSinceEpoch;

    for (const auto& host: report.unreachableHosts)
        inputs.unreachableHosts.push_back({host.host, host.reason});

    return inputs;
}

namespace
{
struct HostCounts
{
    int sessions = 0;
    int panes = 0;
    std::optional<int> daemonProtocolVersion;
};

std::map<std::string, HostCounts> countSessionsByHost(const term::phone::OverviewReport& report)
{
    auto countsByHost = std::map<std::string, HostCounts> {};

    for (const auto& row: report.sessions)
    {
        auto& counts = countsByHost[row.host];
        ++counts.sessions;
        counts.panes += static_cast<int>(row.paneIds.size());

        if (auto* remote = term::RemoteHosts::find(row.host))
            counts.daemonProtocolVersion = remote->getDaemon().getDaemonProtocolVersion();
    }

    return countsByHost;
}

// One line per host: what the phone can open there, or why it cannot reach it.
std::string describeHostsAnswering(const term::phone::OverviewReport& report)
{
    auto text = std::string {};

    for (const auto& [host, counts]: countSessionsByHost(report))
    {
        const auto version = counts.daemonProtocolVersion ? std::to_string(*counts.daemonProtocolVersion) : "unknown";
        text += "\n  " + host + " (daemon protocol " + version + "): " + std::to_string(counts.sessions)
                + " sessions, " + std::to_string(counts.panes) + " panes";
    }

    for (const auto& host: report.unreachableHosts)
        text += "\n  " + host.host + " unreachable: " + host.reason;

    return text;
}
} // namespace

PhoneOverviewBinding::PhoneOverviewBinding(PhoneLaunch& phoneToUse, eacp::Callback showPaneScreenToUse)
    : phone(phoneToUse)
    , showPaneScreen(std::move(showPaneScreenToUse))
{
    phone.getOverview().onRowsChanged = [this](const term::phone::OverviewReport& report) { showReport(report); };
}

void PhoneOverviewBinding::showOn(OverviewScreen& screenToUse)
{
    screen = screenToUse;
    screenToUse.onOpenSession = [this](const term::overview::OverviewRow& row) { openSession(row); };
    screenToUse.setInputs(makeOverviewInputs(phone.getOverview().buildReport(), phone.getSessionOpenings()));
    phone.getOverview().setShown(true);
}

void PhoneOverviewBinding::stopShowing()
{
    screen.reset();
    phone.getOverview().setShown(false);
}

void PhoneOverviewBinding::showReport(const term::phone::OverviewReport& report)
{
    logHostsAnsweringWhenChanged(report);

    if (screen)
        screen->get().setInputs(makeOverviewInputs(report, phone.getSessionOpenings()));

    openNotificationTargetWhenListed(report);

    if (sessionToOpen.empty())
        return;

    if (const auto row = term::phone::findRowByName(report.sessions, sessionToOpen))
    {
        sessionToOpen.clear();
        openSession(*row);
    }
}

void PhoneOverviewBinding::logHostsAnsweringWhenChanged(const term::phone::OverviewReport& report)
{
    auto description = describeHostsAnswering(report);

    if (description == loggedHostsAnswering)
        return;

    LOG("overview: ", report.sessions.size(), " sessions from ", report.hostCount, " hosts:", description);
    loggedHostsAnswering = std::move(description);
}

void PhoneOverviewBinding::openSessionOnceListed(const std::string& hostAndSession)
{
    sessionToOpen = hostAndSession;
}

void PhoneOverviewBinding::openNotificationTargetOnceListed(const term::phone::NotificationTarget& target)
{
    notificationTargetToOpen = target;
}

void PhoneOverviewBinding::openNotificationTargetWhenListed(const term::phone::OverviewReport& report)
{
    if (!notificationTargetToOpen)
        return;

    const auto row = term::phone::findRowForNotification(report.sessions, *notificationTargetToOpen);

    if (!row)
        return;

    const auto paneId = notificationTargetToOpen->paneId;
    notificationTargetToOpen.reset();
    LOG("push: opening ", row->host, "/", row->sessionName, " for pane ", paneId);
    openSession(*row);
    phone.choosePaneOfSession(paneId);
}

std::optional<std::string> PhoneOverviewBinding::openRow(const term::overview::OverviewRow& row)
{
    if (row.isProject)
        return phone.openProject(row);

    return phone.openSession(row);
}

void PhoneOverviewBinding::openSession(const term::overview::OverviewRow& row)
{
    const auto paneId = openRow(row);

    if (!paneId)
        return LOG("overview: ", row.host, "/", row.sessionName, " has no pane its daemon lists");

    LOG("overview: open ", row.host, "/", row.sessionName, " on pane ", *paneId);
    phone.getSessionOpenings().recordOpening(term::overview::makeOverviewRowKey(row),
                                             term::phone::getMillisecondsSinceEpoch());
    stopShowing();
    showPaneScreen();
}
} // namespace cowterm::mobile
