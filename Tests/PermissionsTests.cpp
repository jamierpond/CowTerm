#include "Permissions.h"

#include <NanoTest/NanoTest.h>

#include <set>

// The privacy-grants model behind CowTerm > Permissions…: which grants are
// listed, what each row's button says, and the commands
// the reset and relaunch run.

using namespace nano;
using namespace term::permissions;

auto tEveryKindOnce = test("Permissions/everyKindListedOnce") = []
{
    auto kinds = std::set<Kind> {};
    for (const auto& grant: all())
        kinds.insert(grant.kind);

    check(kinds.size() == all().size());
    check(kinds.size() == 5);
    check(info(Kind::notifications).kind == Kind::notifications);
};

auto tSettingsUrls = test("Permissions/settingsUrls") = []
{
    const auto privacy =
        std::string {"x-apple.systempreferences:com.apple.preference.security?"};

    check(settingsUrl(Kind::accessibility, "") == privacy + "Privacy_Accessibility");
    check(settingsUrl(Kind::fullDiskAccess, "") == privacy + "Privacy_AllFiles");
    check(settingsUrl(Kind::automation, "") == privacy + "Privacy_Automation");
    check(settingsUrl(Kind::filesAndFolders, "") == privacy + "Privacy_FilesAndFolders");
    check(settingsUrl(Kind::notifications, "com.eacp.cowterm")
          == "x-apple.systempreferences:com.apple.Notifications-Settings.extension"
             "?id=com.eacp.cowterm");
};

auto tActionLabels = test("Permissions/actionLabels") = []
{
    const auto& accessibility = info(Kind::accessibility);
    const auto& disk = info(Kind::fullDiskAccess);

    check(actionLabel(accessibility, Status::granted, false).empty());
    check(actionLabel(accessibility, Status::notGranted, false) == "Grant");
    check(actionLabel(accessibility, Status::notGranted, true) == "Open Settings",
          "macOS prompts once; after that only Settings helps");
    check(actionLabel(accessibility, Status::denied, false) == "Open Settings");
    check(actionLabel(disk, Status::notGranted, false) == "Open Settings",
          "no API prompts for Full Disk Access");
};

auto tStatusLabels = test("Permissions/statusLabels") = []
{
    check(statusLabel(Status::granted) == "granted");
    check(statusLabel(Status::notGranted) == "not granted");
};

auto tResetCommand = test("Permissions/resetCommand") = []
{
    check(resetCommand("com.eacp.cowterm")
          == std::vector<std::string> {"/usr/bin/tccutil", "reset", "All", "com.eacp.cowterm"});
    check(resetCommand("com.eacp.cowterm", "Accessibility")[2] == "Accessibility");
};

auto tRelaunchCommand = test("Permissions/relaunchCommand") = []
{
    const auto argv = relaunchCommand(4242, "/Applications/Cow Term.app");

    check(argv.size() == 6);
    check(argv[0] == "/bin/sh" && argv[1] == "-c");
    check(argv[2].find("$1") != std::string::npos && argv[2].find("$2") != std::string::npos);
    check(argv[2].find("Cow Term") == std::string::npos,
          "the path is an argument, never script text");
    check(argv[4] == "4242");
    check(argv[5] == "/Applications/Cow Term.app");
};
