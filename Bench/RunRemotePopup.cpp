// Not a test: opens Ctrl+A i's popup the way AppShell does for a pane on a
// remote host (Popup::show with that host), with no window and no key press,
// and leaves it up long enough for the popup's own trace (COWTERM_POPUP_DEBUG)
// to record what the command drew. Run by scripts/run-remote-popup.sh.
// usage: CowTermRunRemotePopup <host> <directory there>

#include "Popup.h"
#include "RemoteHosts.h"
#include "RemoteLocation.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Threads/EventLoop.h>

#include <cstdio>
#include <string>

using namespace eacp;

namespace
{
constexpr auto popupWidth = 1200.0f;
constexpr auto popupHeight = 760.0f;
constexpr auto waitForHost = Time::MS {30000};
constexpr auto waitPastThePopupScreenTrace = Time::MS {4000};
constexpr auto popupCommand = "lazygit";

int runRemotePopup(const std::string& host, const std::string& directory)
{
    auto config = term::AppConfig {};
    config.searchDirs = {term::remote::makeRemoteProjectDir(host, directory)};
    term::RemoteHosts::configure(config);

    auto* remote = term::RemoteHosts::find(host);
    Threads::runEventLoopUntil([remote] { return remote->getStatus() != term::RemoteHost::Status::Connecting; },
                               waitForHost);

    if (remote->getStatus() != term::RemoteHost::Status::Connected)
    {
        std::fprintf(stderr, "%s: %s\n", host.c_str(), remote->getFailureReason().c_str());
        return 1;
    }

    auto popup = term::Popup {config};
    popup.setBounds({0.0f, 0.0f, popupWidth, popupHeight});
    popup.show(popupCommand, directory, host);
    Threads::runEventLoopFor(waitPastThePopupScreenTrace);
    popup.dismiss();
    Threads::runEventLoopFor(waitPastThePopupScreenTrace);
    term::RemoteHosts::teardown();
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: CowTermRunRemotePopup <host> <directory there>\n");
        return 2;
    }

    const auto host = std::string {argv[1]};
    const auto directory = std::string {argv[2]};
    return Apps::run([&] { Apps::quit(runRemotePopup(host, directory)); });
}
