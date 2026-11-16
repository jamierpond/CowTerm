#include "AppShell.h"
#include "Cli.h"
#include <PondVersion.h>
#include "Multiplexer/DaemonClient.h"
#include "Multiplexer/Protocol.h"
#include "Multiplexer/UsageLog.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Platform/Platform.h>
#include <eacp/Graphics/Menu/Menu.h>
#include <PondApp/SingleInstance.h>

#if EACP_CRASH_REPORTING
#include <eacp/CrashReporting/CrashReporting.h>
#include <eacp/CrashReporting/Faults/Faults.h>
#endif

#include <cstdio>
#include <string_view>
#include <utility>

using namespace eacp;

namespace
{
// A named instance ($COWTERM_INSTANCE) runs beside the user's own CowTerm.
pondapp::SingleInstance& getRunningInstanceClaim()
{
    const auto suffix = term::proto::instanceSuffix();
    static auto claim = pondapp::SingleInstance {
        suffix.empty() ? std::string {"audio.pond.cowterm"} : "audio.pond.cowterm." + suffix};
    return claim;
}

#if EACP_CRASH_REPORTING
// `CowTerm --crash-for-testing <fault>` crashes on purpose with one of Crashout's
// faults (nullptr, abort, stackoverflow, ...), to check a crash report end to end.
constexpr auto crashForTestingFlag = std::string_view {"--crash-for-testing"};

void crashIfAskedToForTesting(int argc, char** argv)
{
    for (auto index = 1; index + 1 < argc; ++index)
    {
        if (argv[index] == crashForTestingFlag)
            CrashReporting::Faults::triggerFault(argv[index + 1],
                                                 CrashReporting::Faults::ThreadChoice::main);
    }
}
#endif

Graphics::WindowOptions windowOptions()
{
    auto options = Graphics::WindowOptions {};
    options.width = 1100;
    options.height = 700;
    options.minWidth = 300;
    options.minHeight = 200;
    options.title = "CowTerm";

    // TODO(linux): options.appId = "audio.pond.cowterm"; — what the desktop
    // matches against audio.pond.cowterm.desktop to label the window and pick
    // its icon; without it every eacp app shows up as "eacp" with a
    // placeholder. WindowOptions::appId only exists in the Linux box's local
    // eacp (upstream Window-Linux.cpp still hardcodes app_id "eacp"); restore
    // this line once that change is pushed and the lockstep pin moved onto it.
    options.backgroundColor =
        term::toColor(term::themeByName(term::loadConfig().theme).background);

    // Closing the window backgrounds the terminal (the tray keeps it
    // reachable); shells and sessions stay alive until an explicit quit.
    options.hidesOnClose = true;
    return options;
}
} // namespace

struct TerminalApp
{
    TerminalApp()
    {
        shell.onWindowTitleChanged = [this](const std::string& title)
        {
            window.setTitle(title.empty() ? "CowTerm  ·  " + pond::getVersionString()
                                          : title);
        };

        shell.onBringToFront = [this]
        {
            term::logUsage("window.show");
            window.setVisible(true);
            window.toFront();
        };

        Apps::setReopenHandler(
            [this]
            {
                window.setVisible(true);
                window.toFront();
            });

        getRunningInstanceClaim().setSecondLaunchHandler(
            [this](const std::vector<std::string>&)
            {
                term::logUsage("app.second-launch");
                window.setVisible(true);
                window.toFront();
            });

        window.events.onActivationChanged = [this](bool isKey)
        { shell.setWindowFocused(isKey); };

        window.events.onHidden = [] { term::logUsage("window.close"); };

        window.setContentView(shell);

        // A Windows window would grow a menu bar of its own, and Linux has no
        // grants to put in one; the tray carries the rest everywhere.
        if constexpr (Platform::isMac())
            installMenuBar();

        // Sessions restore once the daemon dial resolves, so panes with
        // still-running shells re-adopt them instead of spawning fresh.
        term::DaemonClient::initialize([this] { shell.start(); });
    }

    // The standard application menu with the permissions items after About.
    void installMenuBar()
    {
        using Graphics::MenuItem;

        auto showPermissions = [this](bool confirmReset)
        {
            window.setVisible(true);
            window.toFront();
            shell.showPermissions(confirmReset);
        };

        auto app = Graphics::Menu {"CowTerm"};
        auto first = true;

        for (auto& item: Graphics::standardApplicationMenu("CowTerm").items)
        {
            app.add(item);

            if (std::exchange(first, false))
            {
                app.addSeparator();
                app.add(MenuItem::withAction("Permissions…",
                                             [showPermissions] { showPermissions(false); }));
                app.add(MenuItem::withAction("Reset all permissions…",
                                             [showPermissions] { showPermissions(true); }));
            }
        }

        auto bar = Graphics::MenuBar {};
        bar.add(std::move(app));
        Graphics::setApplicationMenuBar(bar, window);
    }

    term::AppShell shell;
    Graphics::Window window {windowOptions()};
};

int main(int argc, char** argv)
{
#if EACP_CRASH_REPORTING
    if (auto handlerExitCode = CrashReporting::runHandlerIfRequested(argc, argv))
        return *handlerExitCode;
#endif

    if (term::isCliInvocation(argc, argv))
        return term::runCli(argc, argv);

    auto& claim = getRunningInstanceClaim();

    if (!claim.isTheRunningInstance()
        && claim.handOverToRunningInstance(pondapp::getLaunchArguments(argc, argv)))
        return pondapp::exitCodeAnotherInstanceIsRunning;

#if EACP_CRASH_REPORTING
    auto crashReportingOptions = CrashReporting::Options {};
    crashReportingOptions.release = pond::getVersionString();
    CrashReporting::start(crashReportingOptions);
    crashIfAskedToForTesting(argc, argv);
#endif

    std::fprintf(stderr, "CowTerm %s\n", pond::getVersionString().c_str());

    term::startUsageLog();
    term::logUsage("app.launch", {{"version", pond::getVersionString()}});
    term::registerEmbeddedFonts();
    return Apps::run<TerminalApp>();
}
