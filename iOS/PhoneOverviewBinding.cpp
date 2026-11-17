#include "PhoneOverviewBinding.h"

#include <eacp/Core/Utils/Logging.h>

namespace cowterm::mobile
{
using namespace eacp;

OverviewInputs makeOverviewInputs(const term::phone::OverviewReport& report)
{
    auto inputs = OverviewInputs {};
    inputs.sessions = report.sessions;
    inputs.hostCount = report.hostCount;
    inputs.nowMilliseconds = report.nowMillisecondsSinceEpoch;

    for (const auto& host: report.unreachableHosts)
        inputs.unreachableHosts.push_back({host.host, host.reason});

    return inputs;
}

PhoneOverviewBinding::PhoneOverviewBinding(PhoneLaunch& phoneToUse,
                                           OverviewScreen& screenToUse,
                                           ShowPane showPaneToUse)
    : phone(phoneToUse)
    , screen(screenToUse)
    , showPane(std::move(showPaneToUse))
{
    phone.getOverview().onRowsChanged = [this](const term::phone::OverviewReport& report) { showReport(report); };

    screen.onOpenSession = [this](const term::overview::OverviewRow& row) { openSession(row); };
}

void PhoneOverviewBinding::showReport(const term::phone::OverviewReport& report)
{
    LOG("overview: ", report.sessions.size(), " sessions, ", report.unreachableHosts.size(), " hosts unreachable");
    screen.setInputs(makeOverviewInputs(report));

    if (sessionToOpen.empty())
        return;

    if (const auto row = term::phone::findRowByName(report.sessions, sessionToOpen))
    {
        sessionToOpen.clear();
        screen.onOpenSession(*row);
    }
}

void PhoneOverviewBinding::openSessionOnceListed(const std::string& hostAndSession)
{
    sessionToOpen = hostAndSession;
}

void PhoneOverviewBinding::setShown(bool isShown)
{
    phone.getOverview().setShown(isShown);
}

void PhoneOverviewBinding::openSession(const term::overview::OverviewRow& row)
{
    const auto paneId = phone.openSession(row);

    if (!paneId)
        return LOG("overview: ", row.host, "/", row.sessionName, " has no pane its daemon lists");

    LOG("overview: open ", row.host, "/", row.sessionName, " on pane ", *paneId);
    setShown(false);
    showPane(*paneId);
}
} // namespace cowterm::mobile
