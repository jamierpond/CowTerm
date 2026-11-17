#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace term::phone
{
// A pane the daemon's info lists, with the session it belongs to.
struct ListedPane
{
    std::string id;
    std::string sessionName;
};

// A pane on one host's daemon.
struct PaneOnHostId
{
    std::string host;
    std::string paneId;

    bool operator==(const PaneOnHostId&) const = default;
};

// One daemon's answer to "last-input": the pane typed into last, and when.
struct LastInput
{
    PaneOnHostId pane;
    std::int64_t millisecondsSinceEpoch = 0;
};

// The pane in host's "last-input [<id> <milliseconds>]" answer; empty when the
// daemon has none, or the message is not that answer.
std::optional<LastInput> parseLastInput(const std::string& host,
                                        const std::string& message);

using IsPaneListed = std::function<bool(const PaneOnHostId&)>;

// The pane the phone opens on when the launch names none: of the panes the
// daemons say were typed into last, the latest; else the pane the phone showed
// last time, if its daemon still lists it. Empty when there is neither: the
// phone never opens a pane nobody chose, since opening one takes its size.
std::optional<PaneOnHostId>
    chooseLaunchPane(const std::vector<LastInput>& lastInputs,
                     const std::optional<PaneOnHostId>& shownLastTime,
                     const IsPaneListed& isListed);

// The ids of sessionName's panes, in the order listed.
std::vector<std::string> listPanesOfSession(const std::vector<ListedPane>& listedPanes,
                                            const std::string& sessionName);
} // namespace term::phone
