#include "Permissions.h"

#include <algorithm>

namespace term::permissions
{
namespace
{
constexpr auto privacyPane = "x-apple.systempreferences:com.apple.preference.security?";
} // namespace

const std::vector<Info>& all()
{
    static const auto grants = std::vector<Info> {
        {Kind::screenRecording,
         "Screen & System Audio Recording",
         "ct screenshot/recordings and agents' screencapture. Applies on relaunch.",
         true,
         "Screen & System Audio Recording",
         true,
         true},
        {Kind::accessibility,
         "Accessibility",
         "Commands that drive other apps: osascript keystrokes, cliclick, agents.",
         false,
         "Accessibility",
         true,
         true},
        {Kind::fullDiskAccess,
         "Full Disk Access",
         "Shells reading protected data: ~/Library/Mail, Safari, Messages.",
         false,
         "Full Disk Access",
         true,
         false},
        {Kind::automation,
         "Automation",
         "osascript / Apple Events to other apps; macOS asks per target app.",
         false,
         "Automation",
         false,
         true},
        {Kind::filesAndFolders,
         "Desktop, Documents & Downloads",
         "Shells in those folders. Not needed once Full Disk Access is on.",
         false,
         "Files & Folders",
         false,
         true},
        {Kind::notifications,
         "Notifications",
         "Ring-back alerts when a session asks for you.",
         false,
         "Notifications",
         false,
         true},
    };

    return grants;
}

const Info& info(Kind kind)
{
    const auto& grants = all();
    return *std::find_if(grants.begin(),
                         grants.end(),
                         [kind](const Info& grant) { return grant.kind == kind; });
}

std::string settingsUrl(Kind kind, const std::string& bundleId)
{
    const auto privacy = [](const char* anchor)
    { return std::string {privacyPane} + anchor; };

    switch (kind)
    {
        case Kind::screenRecording:
            return privacy("Privacy_ScreenCapture");
        case Kind::accessibility:
            return privacy("Privacy_Accessibility");
        case Kind::fullDiskAccess:
            return privacy("Privacy_AllFiles");
        case Kind::automation:
            return privacy("Privacy_Automation");
        case Kind::filesAndFolders:
            return privacy("Privacy_FilesAndFolders");
        case Kind::notifications:
            return "x-apple.systempreferences:com.apple.Notifications-Settings.extension?id="
                   + bundleId;
    }

    return privacy("");
}

bool requiredMissing(const std::vector<Entry>& entries)
{
    return std::any_of(entries.begin(),
                       entries.end(),
                       [](const Entry& entry)
                       {
                           return info(entry.kind).required
                                  && entry.status != Status::granted
                                  && entry.status != Status::needsRelaunch;
                       });
}

bool requiredSatisfied(const std::vector<Entry>& entries)
{
    return std::all_of(entries.begin(),
                       entries.end(),
                       [](const Entry& entry)
                       {
                           return !info(entry.kind).required
                                  || entry.status == Status::granted;
                       });
}

std::string statusLabel(Status status)
{
    switch (status)
    {
        case Status::granted:
            return "granted";
        case Status::needsRelaunch:
            return "granted · relaunch to apply";
        case Status::notGranted:
            return "not granted";
        case Status::denied:
            return "denied";
        case Status::unknown:
            return "asks on first use";
    }

    return {};
}

std::string actionLabel(const Info& grant, Status status, bool prompted)
{
    if (status == Status::granted)
        return {};

    if (status == Status::needsRelaunch)
        return "Relaunch";

    if (grant.canPrompt && !prompted && status != Status::denied)
        return "Grant";

    return "Open Settings";
}

std::vector<std::string> resetCommand(const std::string& bundleId,
                                      const std::string& service)
{
    return {"/usr/bin/tccutil", "reset", service, bundleId};
}

std::vector<std::string> relaunchCommand(int pid, const std::string& bundlePath)
{
    // The app's own pid and path go in as $1/$2, never into the script text.
    return {"/bin/sh",
            "-c",
            R"(while /bin/kill -0 "$1" 2>/dev/null; do /bin/sleep 0.2; done; exec /usr/bin/open "$2")",
            "cowterm-relaunch",
            std::to_string(pid),
            bundlePath};
}
} // namespace term::permissions
