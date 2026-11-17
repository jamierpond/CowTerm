#pragma once

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

// The pane in a "last-input [<id> <milliseconds>]" answer; empty when the
// daemon has none, or the message is not that answer.
std::optional<std::string> parseLastInputPane(const std::string& message);

// Which pane the phone opens: the explicit one when given, else the one last
// typed into, else the first the daemon lists. Empty when there is none.
std::optional<std::string> choosePaneToOpen(const std::string& explicitPaneId,
                                            const std::optional<std::string>& lastInputPane,
                                            const std::vector<ListedPane>& listedPanes);

// The ids of sessionName's panes, in the order listed.
std::vector<std::string> listPanesOfSession(const std::vector<ListedPane>& listedPanes,
                                            const std::string& sessionName);
} // namespace term::phone
