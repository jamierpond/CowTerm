#include "EmbeddedSshProbeArguments.h"

#include <array>
#include <string_view>

namespace
{
struct ModeFlag
{
    std::string_view flag;
    ProbeMode mode;
    bool needsPaneId;
};

constexpr auto modeFlags = std::array {
    ModeFlag {"--adopt", ProbeMode::AdoptPane, false},
    ModeFlag {"--leave-pane", ProbeMode::LeavePaneRunning, true},
    ModeFlag {"--kill-pane", ProbeMode::KillPane, true},
    ModeFlag {"--watch-pane", ProbeMode::WatchPane, true},
    ModeFlag {"--quit-server", ProbeMode::QuitServer, false},
    ModeFlag {"--list-panes", ProbeMode::ListPanes, false},
    ModeFlag {"--list-layouts", ProbeMode::ListLayouts, false},
    ModeFlag {"--list-unread", ProbeMode::ListUnread, false},
};

constexpr auto mostArguments = std::size_t {5};
constexpr auto sessionArgument = std::size_t {2};
constexpr auto folderArgument = std::size_t {3};

std::optional<ModeFlag> findModeFlag(const std::string& text)
{
    for (const auto& flag: modeFlags)
    {
        if (flag.flag == text)
            return flag;
    }

    return std::nullopt;
}

// Only --leave-pane takes a session name and a folder, and the pane modes need a pane id.
bool areModeArgumentsComplete(const ModeFlag& flag, const ProbeArguments& parsed)
{
    const auto hasSessionOrFolder = !parsed.sessionName.empty() || !parsed.folder.empty();
    const auto hasSessionNameWhereNotAllowed = hasSessionOrFolder && flag.mode != ProbeMode::LeavePaneRunning;
    const auto isMissingPaneId = flag.needsPaneId && parsed.paneId.empty();
    return !hasSessionNameWhereNotAllowed && !isMissingPaneId;
}

// The mode and pane after the destination: none at all is ThrowawayPane.
std::optional<ProbeArguments> parseModeArguments(const term::SshDestination& destination,
                                                 const std::vector<std::string>& modeArguments)
{
    if (modeArguments.empty())
        return ProbeArguments {destination};

    const auto flag = findModeFlag(modeArguments[0]);

    if (!flag)
        return std::nullopt;

    auto parsed = ProbeArguments {destination, flag->mode};

    if (modeArguments.size() > 1)
        parsed.paneId = modeArguments[1];

    if (modeArguments.size() > sessionArgument)
        parsed.sessionName = modeArguments[sessionArgument];

    if (modeArguments.size() > folderArgument)
        parsed.folder = modeArguments[folderArgument];

    if (!areModeArgumentsComplete(*flag, parsed))
        return std::nullopt;

    return parsed;
}
} // namespace

std::optional<ProbeArguments> parseProbeArguments(const std::vector<std::string>& arguments)
{
    if (arguments.empty() || arguments.size() > mostArguments)
        return std::nullopt;

    const auto destination = term::parseSshDestination(arguments[0]);

    if (!destination)
        return std::nullopt;

    return parseModeArguments(*destination, {arguments.begin() + 1, arguments.end()});
}
