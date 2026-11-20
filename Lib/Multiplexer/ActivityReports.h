#pragma once

#include "UnreadMark.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace term
{
// What each daemon gave in its last `activity` answer, by host: empty for this
// Mac's own daemon. The Mac's palette and switcher read them.
class ActivityReports
{
public:
    void takeActivityReport(const std::string& host, const std::string& payload);

    // The newest mark among these panes of the host; empty when all are read.
    std::optional<UnreadMark> findNewestUnreadMark(const std::string& host,
                                                   const std::vector<std::string>& paneIds) const;

    // The latest any of these panes was typed into or had a file change in its
    // project, as overview::findLastUseMilliseconds; 0 when none reported one.
    std::int64_t findLastUseMilliseconds(const std::string& host,
                                         const std::vector<std::string>& paneIds) const;

private:
    struct PaneReport
    {
        std::optional<UnreadMark> unread;
        std::int64_t lastUseMilliseconds = 0;
    };

    const std::map<std::string, PaneReport>& findHost(const std::string& host) const;

    std::map<std::string, std::map<std::string, PaneReport>> reportsByHostAndPane;
};
} // namespace term
