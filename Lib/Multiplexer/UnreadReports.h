#pragma once

#include "UnreadMark.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace term
{
// The unread marks each daemon gave in its last `activity` answer, by host:
// empty for this Mac's own daemon. The Mac's palette and switcher read them.
class UnreadReports
{
public:
    void takeActivityReport(const std::string& host, const std::string& payload);

    // The newest mark among these panes of the host; empty when all are read.
    std::optional<UnreadMark> findNewestUnreadMark(const std::string& host,
                                                   const std::vector<std::string>& paneIds) const;

private:
    std::map<std::string, std::map<std::string, UnreadMark>> marksByHostAndPane;
};
} // namespace term
