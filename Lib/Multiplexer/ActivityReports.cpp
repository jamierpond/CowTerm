#include "ActivityReports.h"

#include "SessionOverview.h"

#include <algorithm>

namespace term
{
void ActivityReports::takeActivityReport(const std::string& host, const std::string& payload)
{
    auto& reports = reportsByHostAndPane[host];
    reports.clear();

    for (const auto& activity: overview::parseActivityReport(payload))
        reports[activity.paneId] = {overview::findUnreadMark(activity),
                                    overview::findLastUseMilliseconds(activity),
                                    {activity.lastInputMilliseconds, activity.lastFileChangeMilliseconds}};
}

const std::map<std::string, ActivityReports::PaneReport>&
    ActivityReports::findHost(const std::string& host) const
{
    static const auto noReports = std::map<std::string, PaneReport> {};
    const auto found = reportsByHostAndPane.find(host);
    return found == reportsByHostAndPane.end() ? noReports : found->second;
}

std::optional<UnreadMark> ActivityReports::findNewestUnreadMark(const std::string& host,
                                                                const std::vector<std::string>& paneIds) const
{
    auto newest = std::optional<UnreadMark> {};
    const auto& reports = findHost(host);

    for (const auto& paneId: paneIds)
    {
        if (const auto report = reports.find(paneId); report != reports.end() && report->second.unread)
            newest = chooseNewerUnreadMark(newest, report->second.unread);
    }

    return newest;
}

std::int64_t ActivityReports::findLastUseMilliseconds(const std::string& host,
                                                      const std::vector<std::string>& paneIds) const
{
    auto latest = std::int64_t {0};
    const auto& reports = findHost(host);

    for (const auto& paneId: paneIds)
    {
        if (const auto report = reports.find(paneId); report != reports.end())
            latest = std::max(latest, report->second.lastUseMilliseconds);
    }

    return latest;
}

ActivityReports::PaneActivityTimes ActivityReports::findPaneActivityTimes(const std::string& host,
                                                                          const std::string& paneId) const
{
    const auto& reports = findHost(host);
    const auto report = reports.find(paneId);
    return report == reports.end() ? PaneActivityTimes {} : report->second.times;
}
} // namespace term
