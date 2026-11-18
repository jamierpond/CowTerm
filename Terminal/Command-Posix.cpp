#include "Command.h"
#include "Multiplexer/UsageLog.h"

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Process/Process.h>

#include <chrono>
#include <memory>
#include <thread>

namespace term
{
namespace
{
eacp::Processes::ProcessOptions makeLinkOpenerOptions(const std::string& url)
{
    auto options = eacp::Processes::ProcessOptions {};
    options.executable = eacp::Platform::isMac() ? "/usr/bin/open" : "xdg-open";
    options.arguments.add(url);
    options.mergeErrorOutput = true;
    return options;
}

// The opener exits once it has handed the link to the browser.
void reportWhenLinkOpenerExits(eacp::Processes::Process& opener,
                               std::chrono::steady_clock::time_point clickedAt)
{
    const auto status = opener.wait();
    const auto elapsed = std::chrono::steady_clock::now() - clickedAt;
    const auto millisecondsSinceClick =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    if (status != 0)
    {
        logUsage("link.open-failed",
                 {{"status", status}, {"ms", millisecondsSinceClick}, {"output", opener.output()}});
        return;
    }

    logUsage("link.handed-off", {{"ms", millisecondsSinceClick}});
}
} // namespace

void openInBrowser(const std::string& url)
{
    const auto clickedAt = std::chrono::steady_clock::now();
    logUsage("link.open", {{"url", url}});

    auto opener = std::make_shared<eacp::Processes::Process>(makeLinkOpenerOptions(url));

    if (!opener->launched())
    {
        logUsage("link.open-failed", {{"status", -1}, {"output", "could not start the opener"}});
        return;
    }

    std::thread([opener, clickedAt] { reportWhenLinkOpenerExits(*opener, clickedAt); }).detach();
}
} // namespace term
