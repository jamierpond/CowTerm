#include "CowTermPermissions.h"

#include "CowTermCore/Debug.h"

#include <eacp/Core/App/App.h>

namespace term::permissions
{
using pond::permissions::Info;
using pond::permissions::Kind;

const std::vector<Info>& getCowTermGrants()
{
    static const auto grants = std::vector<Info> {
        {Kind::accessibility,
         "Accessibility",
         "Commands that drive other apps: osascript keystrokes, cliclick, agents.",
         "Accessibility",
         true,
         true},
        {Kind::fullDiskAccess,
         "Full Disk Access",
         "Shells reading protected data: ~/Library/Mail, Safari, Messages.",
         "Full Disk Access",
         true,
         false},
        {Kind::automation,
         "Automation",
         "osascript / Apple Events to other apps; macOS asks per target app.",
         "Automation",
         false,
         true},
        {Kind::filesAndFolders,
         "Desktop, Documents & Downloads",
         "Shells in those folders. Not needed once Full Disk Access is on.",
         "Files & Folders",
         false,
         true},
        {Kind::notifications,
         "Notifications",
         "Ring-back alerts when a session asks for you.",
         "Notifications",
         false,
         true},
    };

    return grants;
}

pond::PermissionsPanelSettings makePermissionsPanelSettings(const AppConfig& config)
{
    const auto theme = themeByName(config.theme);

    return {"CowTerm",
            getCowTermGrants(),
            {"Commands you run in CowTerm get CowTerm's macOS privacy grants, so agents",
             "and scripts can only drive apps or read ~/Library if CowTerm can."},
            "Relaunching keeps every shell running (the daemon holds them).",
            config.font,
            {toColor(theme.foreground),
             toColor(theme.background),
             toColor(theme.selection),
             toColor(theme.ansi[2]),
             toColor(theme.ansi[1]),
             toColor(theme.ansi[3]),
             toColor(theme.ansi[4]),
             toColor(theme.ansi[7]),
             toColor(theme.ansi[8])}};
}

void relaunchCowTerm()
{
    if (!pond::permissions::platform::startRelaunchAfterExit("cowterm-relaunch"))
        return;

    // The ordinary quit: the daemon keeps every shell, and the new process
    // re-adopts them on launch.
    noteQuit("permissions relaunch");
    eacp::Apps::quit();
}
} // namespace term::permissions
