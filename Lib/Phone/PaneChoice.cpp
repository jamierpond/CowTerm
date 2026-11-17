#include "PaneChoice.h"

#include "Multiplexer/Protocol.h"

namespace term::phone
{
std::optional<std::string> parseLastInputPane(const std::string& message)
{
    const auto parsed = proto::parse(message);

    if (parsed.verb != "last-input" || parsed.args.empty())
        return std::nullopt;

    return parsed.args[0];
}

std::optional<std::string> choosePaneToOpen(const std::string& explicitPaneId,
                                            const std::optional<std::string>& lastInputPane,
                                            const std::vector<ListedPane>& listedPanes)
{
    if (!explicitPaneId.empty())
        return explicitPaneId;

    if (lastInputPane)
        return lastInputPane;

    if (listedPanes.empty())
        return std::nullopt;

    return listedPanes.front().id;
}

std::vector<std::string> listPanesOfSession(const std::vector<ListedPane>& listedPanes,
                                            const std::string& sessionName)
{
    auto ids = std::vector<std::string> {};

    for (const auto& pane: listedPanes)
    {
        if (pane.sessionName == sessionName)
            ids.push_back(pane.id);
    }

    return ids;
}
} // namespace term::phone
