#include "UnreadReports.h"

#include "SessionOverview.h"

namespace term
{
void UnreadReports::takeActivityReport(const std::string& host, const std::string& payload)
{
    auto& marks = marksByHostAndPane[host];
    marks.clear();

    for (const auto& activity: overview::parseActivityReport(payload))
    {
        if (const auto mark = overview::findUnreadMark(activity))
            marks[activity.paneId] = *mark;
    }
}

std::optional<UnreadMark> UnreadReports::findNewestUnreadMark(const std::string& host,
                                                              const std::vector<std::string>& paneIds) const
{
    const auto found = marksByHostAndPane.find(host);

    if (found == marksByHostAndPane.end())
        return std::nullopt;

    auto newest = std::optional<UnreadMark> {};

    for (const auto& paneId: paneIds)
    {
        if (const auto mark = found->second.find(paneId); mark != found->second.end())
            newest = chooseNewerUnreadMark(newest, mark->second);
    }

    return newest;
}
} // namespace term
